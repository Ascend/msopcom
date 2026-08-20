/* -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2025 Huawei Technologies Co.,Ltd.
 *
 * MindStudio is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *          http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * ------------------------------------------------------------------------- */

#include <iostream>

#include "DevMemManager.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/HijackedLayerManager.h"
#include "MemoryDataCollect.h"
#include "ArgsManager.h"
#include "utils/InjectLogger.h"
#include "utils/Protocol.h"
#include "InteractHelper.h"
#include "utils/Numeric.h"
#include "utils/Ustring.h"
#include "MemGuard.h"
#include "LaunchInitFinalize.h"

KernelType GetCurrentKernelType() {
    uint64_t launchId = KernelContext::Instance().GetLaunchId();
    uint64_t registerId = KernelContext::Instance().GetRegisterId(launchId);
    KernelType kernelType{};
    KernelContext::RegisterEvent registerEvent;
    KernelContext::LaunchEvent launchEvnet{};
    if (KernelContext::Instance().GetRegisterEvent(registerId, registerEvent) &&
        KernelContext::Instance().GetLaunchEvent(launchId, launchEvnet)) {
        kernelType = GetKernelType(registerEvent, launchEvnet);
    }
    return kernelType;
}

KernelType GetKernelType(
    KernelContext::RegisterEvent const &registerEvent, KernelContext::LaunchEvent const &launchEvent) {
    auto kernelType = MagicToKernelType(registerEvent.bin.magic);
    if (kernelType != KernelType::MIX) {
        return kernelType;
    }

    std::string kernelName = launchEvent.kernelName;
    RemoveSuffix(kernelName, {MIX_AIC_SUFFIX, MIX_AIV_SUFFIX});
    bool findKernelMixAic{};
    bool findKernelMixAiv{};
    for (auto const &k : registerEvent.kernelNames) {
        if (kernelName + MIX_AIC_SUFFIX == k) {
            findKernelMixAic = true;
        } else if (kernelName + MIX_AIV_SUFFIX == k) {
            findKernelMixAiv = true;
        }
        // find both aic and aiv kernels, then break loop
        if (findKernelMixAic && findKernelMixAiv) {
            break;
        }
    }
    if (!findKernelMixAiv) {
        return KernelType::AICUBE;
    } else if (!findKernelMixAic) {
        return KernelType::AIVEC;
    } else {
        return KernelType::MIX;
    }
}

namespace {
std::mutex g_instrMutex;

using namespace OnlineShadowMemory;

inline bool InAclNewLaunchCallStack();

inline bool IsC220Arch(DeviceType deviceType) {
    return deviceType > DeviceType::ASCEND_910B_START && deviceType < DeviceType::ASCEND_910B_END;
}

inline bool IsC310Arch(DeviceType deviceType) {
    return deviceType > DeviceType::ASCEND_950_START && deviceType < DeviceType::ASCEND_950_END;
}

inline bool HasSubBlocks(DeviceType deviceType) { return IsC220Arch(deviceType) || IsC310Arch(deviceType); }

inline bool SupportSimt(DeviceType deviceType) { return IsC310Arch(deviceType); }

inline uint64_t GetAllThreadSize(RecordGlobalHead const &globalHead) {
    return (globalHead.offsetInfo.simtErrorInfo.size + sizeof(SimtRecordBlockHead)) * SIMT_THREAD_MAX_SIZE;
}

inline uint64_t GetRecordHeadSize(uint32_t hostMemoryNum) {
    return CeilByAlignSize<CACHE_LINE_SIZE>(sizeof(RecordBlockHead) + hostMemoryNum * sizeof(HostMemoryInfo));
}

inline bool AssignMemSize(RecordGlobalHead &head, uint64_t blockDim, DeviceType deviceType) {
    std::lock_guard<std::mutex> guard(g_instrMutex);
    // calculate total block dim
    uint64_t totalBlockDim{};
    uint64_t totalCacheSize{};
    uint64_t singleBlockSize = head.checkParms.cacheSize;
    if (HasSubBlocks(deviceType)) {
        totalBlockDim = C220_MIX_SUB_BLOCKDIM * blockDim;
        const uint8_t vecCubeSum = 2;
        totalCacheSize = head.checkParms.checkBlockId == CHECK_ALL_BLOCK
            ? singleBlockSize * totalBlockDim
            : singleBlockSize * vecCubeSum + SINGLE_CHECK_OTHER_BLOCK_CACHE_SIZE * (totalBlockDim - vecCubeSum);
    } else {
        totalBlockDim = blockDim;
        totalCacheSize = head.checkParms.checkBlockId == CHECK_ALL_BLOCK
            ? totalBlockDim * singleBlockSize
            : singleBlockSize + SINGLE_CHECK_OTHER_BLOCK_CACHE_SIZE * (totalBlockDim - 1);
    }
    DEBUG_LOG("sanitizer init with blockDim: %lu, totalBlockDim: %lu", blockDim, totalBlockDim);
    head.kernelInfo.totalBlockDim = totalBlockDim;
    head.kernelInfo.totalCacheSize = totalCacheSize;
    if (singleBlockSize == 0 || singleBlockSize > MAX_RECORD_BUF_SIZE_EACH_BLOCK ||
        totalCacheSize > MAX_RECORD_BUF_SIZE_ALL_BLOCK) {
        ERROR_LOG("cache size must be [1, 8192](MB), total cache size must less than %u"
                  "MB, but current cache size is %lu MB, total cache size is %lu MB",
            MAX_RECORD_BUF_SIZE_ALL_BLOCK, singleBlockSize, totalCacheSize);
        return false;
    }
    return true;
}

inline bool IsTargetBlockIdx(int16_t checkBlockId, uint64_t blockIdx) {
    DeviceType deviceType = GetDeviceTypeBySocVersion(DeviceContext::Local().GetSocVersion());
    if (HasSubBlocks(deviceType)) {
        uint64_t vecTargetBlockIdx =
            checkBlockId / C220_VEC_SUB_BLOCKDIM * C220_MIX_SUB_BLOCKDIM + checkBlockId % C220_VEC_SUB_BLOCKDIM;
        uint64_t cubeTargetBlockIdx = checkBlockId * C220_MIX_SUB_BLOCKDIM + C220_VEC_SUB_BLOCKDIM;
        return blockIdx == vecTargetBlockIdx || blockIdx == cubeTargetBlockIdx;
    } else {
        return blockIdx == static_cast<uint64_t>(checkBlockId);
    }
}

inline uint64_t GetBlockSize(const RecordGlobalHead &globalHead, uint64_t blockIdx) {
    auto checkBlockId = globalHead.checkParms.checkBlockId;
    auto cacheSize = globalHead.checkParms.cacheSize;
    if (checkBlockId == CHECK_ALL_BLOCK || IsTargetBlockIdx(checkBlockId, blockIdx)) {
        return globalHead.offsetInfo.blockHeadSize + cacheSize * MB_TO_BYTES;
    } else {
        return globalHead.offsetInfo.blockHeadSize + SINGLE_CHECK_OTHER_BLOCK_CACHE_SIZE * MB_TO_BYTES;
    }
}

void SendKernelBlock(const std::string &body, int curBlkIdx, int totalBlkIdx) {
    // report kernel records

    if (curBlkIdx + 1 == totalBlkIdx) {
        // 仅最后一个block时同步
        DEBUG_LOG("Interact of the last kernel block.");
        KernelRecordResponse response{};
        constexpr uint32_t timeOutForLastBlock = 600;
        bool interactGood = InteractHelper::Interact<std::string, KernelRecordResponse, timeOutForLastBlock>(
            PacketType::KERNEL_RECORD, body, response);
        if (!interactGood) {
            std::string kernelName = KernelContext::Instance().GetLaunchName();
            ERROR_LOG("Error occured when doing block interact for kernel %s", kernelName.c_str());
        }
    } else {
        int32_t deviceId = 0;
        aclrtGetDeviceImplOrigin(&deviceId);
        InteractHelper::Notify(deviceId, PacketType::KERNEL_RECORD, body);
    }
}

void ReportParaBase(RecordBlockHead const &head, ParaBaseRegister &paraBase, bool &isReportParaBase) {
    const auto &paraBaseInfo = head.paraBase;
    if (!isReportParaBase && paraBaseInfo.addr != ILLEGAL_ADDR && paraBaseInfo.size != 0) {
        ReportMalloc(paraBaseInfo.addr, paraBaseInfo.size, MemInfoSrc::BYPASS, MemInfoDesc::PARA_BASE);
        ReportMemset(paraBaseInfo.addr, paraBaseInfo.size, MemInfoSrc::BYPASS, MemInfoDesc::PARA_BASE);
        paraBase = paraBaseInfo;
        isReportParaBase = true;
    }
}

/// 落盘在线检测对应的malloc内存，便于后续定位问题
void DumpMemoryInfo(uint8_t *memInfoHost, const uint8_t *memInfo, uint64_t blockRemainSize, uint64_t blockIdx,
    const RecordGlobalHead &globalHead) {
    aclError error = aclrtMemcpyImplOrigin(
        memInfoHost, blockRemainSize, memInfo, sizeof(RecordBlockHead), ACL_MEMCPY_DEVICE_TO_HOST);
    auto *blockHead = reinterpret_cast<RecordBlockHead *>(memInfoHost);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy hostMemInfo error: %d", error);
        return;
    }
    if (blockHead->hostMemoryNum == 0U) {
        return;
    }
    uint64_t recordHeadSize = globalHead.offsetInfo.blockHeadSize;
    if (recordHeadSize > blockRemainSize) {
        DEBUG_LOG("too much host memory");
        return;
    };
    blockHead->hostMemoryInfoPtr = reinterpret_cast<HostMemoryInfo *>(blockHead + 1);
    error = aclrtMemcpyImplOrigin(memInfoHost + sizeof(RecordBlockHead), blockRemainSize - sizeof(RecordBlockHead),
        memInfo + sizeof(RecordBlockHead), recordHeadSize - sizeof(RecordBlockHead), ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy hostMemInfo error: %d", error);
        return;
    }
    for (size_t i = 0; i < blockHead->hostMemoryNum; ++i) {
        auto memory = blockHead->hostMemoryInfoPtr[i];
        DEBUG_LOG("online memcheck in block:%lu addr:0x%lx size:%lu", blockIdx, memory.addr, memory.size);
    }
}

std::vector<HostMemoryInfo> CopyExtraMallocToHostMemory() {
    std::vector<HostMemoryInfo> hostMems = MemoryManage::Instance().MergeHostMems();
    const auto &opMemInfo = KernelContext::Instance().GetOpMemInfo();
    auto &inputAddrs = opMemInfo.inputParamsAddrInfos;
    size_t extraIndex = 0;
    for (const auto &mems : hostMems) {
        if (mems.addr == 0x0 && mems.size == 0x0) {
            break;
        }
        extraIndex++;
    }
    size_t hostMemSize = hostMems.size();
    size_t inputSize = inputAddrs.size();
    for (size_t i = 0; i < inputSize; ++i) {
        if (extraIndex + i >= hostMemSize) {
            ERROR_LOG("The index range exceeds the size limit of input hostMems. index: %ld max size: %ld",
                extraIndex + i, hostMemSize);
            break;
        }
        hostMems[extraIndex + i].size = inputAddrs[i].length;
    }
    if (opMemInfo.tilingDataSize > 0) {
        if (extraIndex + inputSize < hostMemSize) {
            hostMems[extraIndex + inputSize].size = opMemInfo.tilingDataSize;
        } else {
            ERROR_LOG("The index range exceeds the size limit of tiling hostMems. index: %ld max size: %ld",
                extraIndex + inputSize, hostMemSize);
        }
    }
    return hostMems;
}

bool InitSimtShadowMemoryHead(const RecordGlobalHead &recordGlobalHead, uint8_t *memInfo) {
    uint64_t shadowMemorySize = recordGlobalHead.offsetInfo.shadowMemoryInfo.size;
    uint8_t *shadowMemoryHeadAddr = memInfo + recordGlobalHead.offsetInfo.shadowMemoryInfo.offset;

    aclError error = aclrtMemsetImplOrigin(shadowMemoryHeadAddr, shadowMemorySize, 0x00, shadowMemorySize);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("init memset 0 to shadow memory heap error: %d", error);
        return false;
    }

    ShadowMemoryHeapHead smHeapHead;
    smHeapHead.startAddr = (uint64_t)shadowMemoryHeadAddr + sizeof(ShadowMemoryHeapHead);
    smHeapHead.size =
        shadowMemorySize > sizeof(ShadowMemoryHeapHead) ? shadowMemorySize - sizeof(ShadowMemoryHeapHead) : 0U;
    // 预分配shadow memory L0级表空间，每个节点占8字节
    smHeapHead.current = smHeapHead.startAddr +
        ((ONLINE_GLOBAL_MEM_MASK + ONLINE_LOCAL_MEM_MASK - 1U) / ONLINE_LOCAL_MEM_MASK) * sizeof(uint64_t);
    smHeapHead.lock = 0U;

    DEBUG_LOG("ShadowMemoryHeapHead on 0x%lx startAddr=0x%lx size=0x%lx current=0x%lx lock=0x%lx",
        reinterpret_cast<uint64_t>(shadowMemoryHeadAddr), smHeapHead.startAddr, smHeapHead.size, smHeapHead.current,
        smHeapHead.lock);

    error = aclrtMemcpyImplOrigin(shadowMemoryHeadAddr, sizeof(ShadowMemoryHeapHead),
        static_cast<const void *>(&smHeapHead), sizeof(ShadowMemoryHeapHead), ACL_MEMCPY_HOST_TO_DEVICE);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("init shadow memory heap head error: %d", error);
        return false;
    }

    return true;
}

inline bool InAclNewLaunchCallStack() {
    return HijackedLayerManager::Instance().InCallStack("aclrtLaunchKernelImpl") ||
        HijackedLayerManager::Instance().InCallStack("aclrtLaunchKernelV2Impl") ||
        HijackedLayerManager::Instance().InCallStack("aclrtLaunchKernelWithConfigImpl") ||
        HijackedLayerManager::Instance().InCallStack("aclrtLaunchKernelWithHostArgsImpl");
}

// 主要用于存放sanitizer_init和sanitizer_finalize需要共享的数据
struct SanitizerLaunchGlobalData {
    Register registers[MAX_AICORE_NUM]; // 保存寄存器状态，向下一个算子传递

    static SanitizerLaunchGlobalData &GetInstance() {
        static SanitizerLaunchGlobalData inst;
        return inst;
    }
};

} // namespace [Dummy]

void SanitizerLaunchInit::Init(uint64_t blockDim) {
    blockDim_ = blockDim;
    hostMemoryNum_ = MemoryManage::Instance().GetMallocCount();
    InitKernelType();
}

void SanitizerLaunchInit::InitKernelType() {
    if (InAclNewLaunchCallStack()) {
        LaunchContextSP launchCtx = LaunchManager::Local().GetLastContext();
        if (launchCtx) {
            const auto &funcContext = launchCtx->GetFuncContext();
            kernelType_ = funcContext->GetKernelType();
        }
    } else {
        kernelType_ = GetCurrentKernelType();
    }
}

bool SanitizerLaunchInit::AssignGlobalHead() {
    // 继承上一个算子的寄存器状态
    aclError error = aclrtMemcpyImplOrigin(globalHead_.registers, sizeof(Register) * MAX_AICORE_NUM,
        SanitizerLaunchGlobalData::GetInstance().registers, sizeof(Register) * MAX_AICORE_NUM,
        ACL_MEMCPY_HOST_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("init memcpy registers error: %d", error);
        return false;
    }

    // 检查是否需要双页表地址还原，如是则配置偏移地址
    DeviceType deviceType = GetDeviceTypeBySocVersion(DeviceContext::Local().GetSocVersion());
    if (IsC220Arch(deviceType)) {
        rtError_t err =
            rtGetL2CacheOffsetOrigin(DeviceContext::Local().GetDeviceId(), &globalHead_.kernelInfo.l2CacheOffset);
        if (err != RT_ERROR_NONE) {
            ERROR_LOG("Get L2Cache Offset failed, ret is %d.", err);
            return false;
        }
        DEBUG_LOG("GlobalHead get L2Cache Offset = %lu", globalHead_.kernelInfo.l2CacheOffset);
    } else {
        globalHead_.kernelInfo.l2CacheOffset = 0;
    }

    SanitizerConfig cliConfig = SanitizerConfigManager::Instance().GetConfig();
    globalHead_.checkParms.cacheSize = cliConfig.cacheSize;
    globalHead_.checkParms.checkBlockId = cliConfig.checkBlockId;
    globalHead_.checkParms.defaultcheck = cliConfig.defaultCheck;
    globalHead_.checkParms.memcheck = cliConfig.memCheck;
    globalHead_.checkParms.outOfBoundCheck = cliConfig.outOfBoundCheck;
    globalHead_.checkParms.racecheck = cliConfig.raceCheck;
    globalHead_.checkParms.initcheck = cliConfig.initCheck;
    globalHead_.checkParms.synccheck = cliConfig.syncCheck;
    globalHead_.checkParms.registerCheck = cliConfig.registerCheck;
    globalHead_.checkParms.dcciCheck = cliConfig.checkDcci;
    globalHead_.checkParms.gmBufferGuardSize = cliConfig.gmBufferGuardSize;
    globalHead_.kernelInfo.kernelType = kernelType_;
    globalHead_.kernelInfo.kernelParamNum = KernelContext::Instance().GetKernelParamNum();
    globalHead_.supportSimt = SupportSimt(deviceType);
    globalHead_.offsetInfo.blockHeadSize = GetRecordHeadSize(hostMemoryNum_);
    DEBUG_LOG("offsetInfo.blockHeadSize:%u cacheByteSize:%lu", globalHead_.offsetInfo.blockHeadSize,
        globalHead_.checkParms.cacheSize * MB_TO_BYTES);
    if (globalHead_.supportSimt) {
        AssignSimtVfInfo();
    }
    return AssignMemSize(globalHead_, blockDim_, deviceType);
}

void SanitizerLaunchInit::AssignSimtVfInfo() {
    auto &offsetInfo = globalHead_.offsetInfo;
    uint64_t cacheByteSize = globalHead_.checkParms.cacheSize * MB_TO_BYTES;
    uint64_t simtInstrSize = static_cast<uint64_t>(cacheByteSize * SIMT_CACHE_SIZE_RATIO / SIMT_THREAD_MAX_SIZE);
    if (simtInstrSize >= sizeof(SimtRecordBlockHead)) {
        offsetInfo.simtErrorInfo.size = AlignDownSize<CACHE_LINE_SIZE>(simtInstrSize - sizeof(SimtRecordBlockHead));
    } else {
        uint32_t needCacheSize = sizeof(SimtRecordBlockHead) *
                static_cast<uint32_t>(SIMT_THREAD_MAX_SIZE / SIMT_CACHE_SIZE_RATIO / MB_TO_BYTES) +
            1;
        ERROR_LOG("cache-size:%u is too small, need cache-size=%u", globalHead_.checkParms.cacheSize, needCacheSize);
        return;
    }
    // 1. 先赋值ubDynamicSize
    uint32_t ubDynamicSize = InAclNewLaunchCallStack() ? LaunchManager::Local().GetSimtUbDynamicSize()
                                                       : KernelContext::Instance().GetSimtUbDynamicSize();
    DEBUG_LOG("simtInfo.ubDynamicSize:%u", ubDynamicSize);
    constexpr uint32_t ubDynamicAlignSize = 128;
    globalHead_.simtInfo.ubDynamicSize = CeilByAlignSize<ubDynamicAlignSize>(ubDynamicSize);

    // 2. 设置simtErrorInfo相关的偏移信息，主要用于simt在线检测错误记录协议
    uint64_t simtErrorInfoOffset = static_cast<uint64_t>(
        cacheByteSize * (1 - SIMT_CACHE_SIZE_RATIO - SHADOW_MEM_CACHE_SIZE_RATIO - SIMT_ENTRY_CACHE_SIZE_RATIO));
    offsetInfo.simtErrorInfo.offset = AlignDownSize<CACHE_LINE_SIZE>(simtErrorInfoOffset) + offsetInfo.blockHeadSize;
    DEBUG_LOG("offsetInfo simtErrorInfo.offset=%lu simtErrorInfo.size=%lu", offsetInfo.simtErrorInfo.offset,
        offsetInfo.simtErrorInfo.size);

    // 3. 设置shadowMemoryInfo相关的偏移信息，主要用于simt在线检测对应的shadowMemory记录协议
    uint64_t shadowMemorySize = static_cast<uint64_t>(cacheByteSize * SHADOW_MEM_CACHE_SIZE_RATIO);
    if (shadowMemorySize < SHADOW_MEM_MIN_BYTE_SIZE) {
        WARN_LOG(
            "overlapping detection between threads is disabled. cache-size:%u is too small, need cache-size >= %lu",
            globalHead_.checkParms.cacheSize,
            static_cast<uint64_t>(SHADOW_MEM_MIN_BYTE_SIZE / MB_TO_BYTES / SHADOW_MEM_CACHE_SIZE_RATIO));
        offsetInfo.shadowMemoryInfo.offset = 0U;
        offsetInfo.shadowMemoryInfo.size = 0U;
    } else {
        offsetInfo.shadowMemoryInfo.offset =
            AlignDownSize<CACHE_LINE_SIZE>(offsetInfo.simtErrorInfo.offset + GetAllThreadSize(globalHead_)) +
            CACHE_LINE_SIZE;
        offsetInfo.shadowMemoryInfo.size = shadowMemorySize;
        DEBUG_LOG("offsetInfo shadowMemoryInfo.offset=%lu shadowMemoryInfo.size=%lu",
            offsetInfo.shadowMemoryInfo.offset, offsetInfo.shadowMemoryInfo.size);
    }

    // 4. 设置simtEntryInfo相关的偏移信息，主要用于将simt在线检测对应的shadowMemory信息拷贝到simtEntryInfo偏移位置
    uint64_t simtEntrySize = static_cast<uint64_t>(cacheByteSize * SIMT_ENTRY_CACHE_SIZE_RATIO) - CACHE_LINE_SIZE;
    offsetInfo.simtEntryInfo.offset =
        AlignDownSize<CACHE_LINE_SIZE>(offsetInfo.shadowMemoryInfo.offset + offsetInfo.shadowMemoryInfo.size) +
        CACHE_LINE_SIZE;
    // 判断尾部是否越界，如果越界则尾部预留一个CACHE_LINE_SIZE，防止和下一个核开头cache_line冲突
    if (simtEntrySize + offsetInfo.simtEntryInfo.offset >= cacheByteSize + offsetInfo.blockHeadSize) {
        simtEntrySize = cacheByteSize + offsetInfo.blockHeadSize - offsetInfo.simtEntryInfo.offset - CACHE_LINE_SIZE;
    }
    offsetInfo.simtEntryInfo.size = simtEntrySize;
    DEBUG_LOG("offsetInfo simtEntryInfo.offset=%lu simtEntryInfo.size=%lu", offsetInfo.simtEntryInfo.offset,
        offsetInfo.simtEntryInfo.size);
}

bool SanitizerLaunchInit::BlockHeadsH2D(uint8_t *memInfo) const {
    aclError error;
    // 1. 初始化 RecordGlobalHead
    error = aclrtMemcpyImplOrigin(memInfo, sizeof(globalHead_), static_cast<const void *>(&globalHead_),
        sizeof(globalHead_), ACL_MEMCPY_HOST_TO_DEVICE);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("init memcpy record global head error: %d", error);
        return false;
    }
    memInfo += sizeof(globalHead_);

    RecordBlockHead recordBlockHead{};
    if (InAclNewLaunchCallStack()) {
        recordBlockHead.paraBase.size = ArgsManager::Instance().GetArgsSize();
    } else {
        recordBlockHead.paraBase.size = KernelContext::Instance().GetArgsSize();
    }
    recordBlockHead.hostMemoryNum = hostMemoryNum_;
    for (size_t blockIdx = 0; blockIdx < globalHead_.kernelInfo.totalBlockDim; ++blockIdx) {
        // 2. 初始化每个核的 RecordBlockHead
        recordBlockHead.hostMemoryInfoPtr = recordBlockHead.hostMemoryNum > 0
            ? reinterpret_cast<HostMemoryInfo *>(memInfo + sizeof(RecordBlockHead))
            : nullptr;
        error = aclrtMemcpyImplOrigin(memInfo, sizeof(RecordBlockHead), static_cast<const void *>(&recordBlockHead),
            sizeof(RecordBlockHead), ACL_MEMCPY_HOST_TO_DEVICE);
        if (error != ACL_ERROR_NONE) {
            ERROR_LOG("init memcpy simd record block head error: %d", error);
            return false;
        }

        // 3. 初始化每个核RecordBlockHead中的hostMemoryInfoPtr
        if (!HostMallocH2D(memInfo, blockIdx)) {
            return false;
        }

        // 4. 初始化simtRecordHead
        if (!SimtBlockHeadsH2D(memInfo, blockIdx)) {
            return false;
        }

        // 5. 偏移memInfo到下一个核的开头
        memInfo += GetBlockSize(globalHead_, blockIdx);
    }
    return true;
}

bool SanitizerLaunchInit::HostMallocH2D(uint8_t *memInfo, size_t blockIdx) const {
    if (!globalHead_.supportSimt) {
        return true;
    }
    std::vector<HostMemoryInfo> hostMems = CopyExtraMallocToHostMemory();
    size_t memsSize = hostMems.size() * sizeof(HostMemoryInfo);
    // 3. 初始化每个核RecordBlockHead中的hostMemoryInfoPtr
    if (hostMems.size() != 0U) {
        aclError error = aclrtMemcpyImplOrigin(memInfo + sizeof(RecordBlockHead), memsSize,
            static_cast<const void *>(hostMems.data()), memsSize, ACL_MEMCPY_HOST_TO_DEVICE);
        if (error != ACL_ERROR_NONE) {
            ERROR_LOG("init block: %ld host malloc mem to record block head error: %d", blockIdx, error);
            return false;
        }
    }
    return true;
}

bool SanitizerLaunchInit::SimtBlockHeadsH2D(uint8_t *memInfo, size_t blockIdx) const {
    if (!globalHead_.supportSimt) {
        return true;
    }

    int16_t checkBlockId = globalHead_.checkParms.checkBlockId;
    if (checkBlockId == CHECK_ALL_BLOCK || IsTargetBlockIdx(checkBlockId, blockIdx)) {
        uint64_t allThreadSize = GetAllThreadSize(globalHead_);
        aclError error = aclrtMemsetImplOrigin(
            memInfo + globalHead_.offsetInfo.simtErrorInfo.offset, allThreadSize, 0x00, allThreadSize);
        if (error != ACL_ERROR_NONE) {
            ERROR_LOG("init memset simt record block head error: %d", error);
            return false;
        }

        // 初始化 shadow memory
        if (globalHead_.offsetInfo.shadowMemoryInfo.size == 0U) {
            DEBUG_LOG("shadow memory disabled for block %lu", blockIdx);
        } else {
            if (!InitSimtShadowMemoryHead(globalHead_, memInfo)) {
                ERROR_LOG("init shadow memory head error for block: %lu", blockIdx);
                return false;
            }
        }
    }
    return true;
}

uint8_t *SanitizerLaunchInit::SkipKernelProcess() const {
    auto &devMemManager = DevMemManager::Instance();
    void *memPtr = nullptr;
    // 对于不需要检测的kernel，申请1M即可，优化整体检测耗时
    uint64_t memSize = MB_TO_BYTES;
    aclError error = devMemManager.MallocMemory(&memPtr, memSize);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("init malloc memInfo error: %d size:%lu", error, memSize);
        return nullptr;
    }
    auto *memInfo = static_cast<uint8_t *>(memPtr);
    return memInfo;
}

uint8_t *SanitizerLaunchInit::Process() {
    auto &devMemManager = DevMemManager::Instance();
    if (devMemManager.IsSkipKernel()) {
        return SkipKernelProcess();
    }

    if (!AssignGlobalHead()) {
        return nullptr;
    }

    uint64_t memSize = globalHead_.kernelInfo.totalCacheSize * MB_TO_BYTES + sizeof(RecordGlobalHead) +
        globalHead_.kernelInfo.totalBlockDim * globalHead_.offsetInfo.blockHeadSize;
    void *memPtr = nullptr;
    aclError error = devMemManager.MallocMemory(&memPtr, memSize);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("init malloc memInfo error: %d size:%lu", error, memSize);
        return nullptr;
    }

    auto *memInfo = static_cast<uint8_t *>(memPtr);
    if (!BlockHeadsH2D(memInfo)) {
        devMemManager.Free();
        return nullptr;
    }

    if (devMemManager.IsMemoryInit()) {
        DEBUG_LOG("memInfo is already initialized, skip init");
        return memInfo;
    }

    devMemManager.SetMemoryInitFlag(true);
    return memInfo;
}

void SanitizerLaunchFinalize::Init(const uint8_t *memInfo, uint64_t blockDim) {
    memInfo_ = memInfo;
    memInfoBackUp_ = memInfo_;
    blockDim_ = blockDim;
    blockSize_ = SanitizerConfigManager::Instance().GetConfig().cacheSize * MB_TO_BYTES + sizeof(RecordGlobalHead) +
        sizeof(RecordBlockHead);
    blockRemainSize_ = blockSize_;
    sendMemorySize_ = 0;
}

void SanitizerLaunchFinalize::UpdateOffsetStatus(uint64_t offset, uint64_t d2HOffsetDiff) {
    memInfo_ += offset + d2HOffsetDiff;
    memInfoHost_ += offset;
    sendMemorySize_ += offset;
    blockRemainSize_ -= offset;
}

void SanitizerLaunchFinalize::ResetOffsetStatus(size_t blockIdx) {
    memInfoBackUp_ += GetBlockSize(*globalHead_, blockIdx);
    memInfo_ = memInfoBackUp_;
    memInfoHost_ = memInfoHostBackUp_ + sizeof(RecordGlobalHead);
    blockRemainSize_ = blockSize_;
    sendMemorySize_ = sizeof(RecordGlobalHead);
}

bool SanitizerLaunchFinalize::GlobalHeadD2H() {
    aclError error = aclrtMemcpyImplOrigin(
        memInfoHost_, blockRemainSize_, memInfo_, sizeof(RecordGlobalHead), ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy RecordGlobalHead error: %d", error);
        return false;
    }
    globalHead_ = reinterpret_cast<RecordGlobalHead *>(memInfoHost_);
    memInfoBackUp_ += sizeof(RecordGlobalHead);
    UpdateOffsetStatus(sizeof(RecordGlobalHead));
    return true;
}

bool SanitizerLaunchFinalize::RegistersD2H() const {
    aclError error = aclrtMemcpyImplOrigin(SanitizerLaunchGlobalData::GetInstance().registers,
        sizeof(Register) * MAX_AICORE_NUM, globalHead_->registers, sizeof(Register) * MAX_AICORE_NUM,
        ACL_MEMCPY_HOST_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy registers error: %d", error);
        return false;
    }
    return true;
}

bool SanitizerLaunchFinalize::BlockHeadD2H(size_t blockIdx) {
    aclError error = aclrtMemcpyImplOrigin(
        memInfoHost_, blockRemainSize_, memInfo_, sizeof(RecordBlockHead), ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy RecordBlockHead error: %d", error);
        return false;
    }
    blockHead_ = reinterpret_cast<RecordBlockHead *>(memInfoHost_);
    DEBUG_LOG("get recordHead from subBlock %lu, offset:%lu, writeOffset:%lu recordCount:%lu, recordWriteCount:%lu",
        blockIdx, blockHead_->offset, blockHead_->writeOffset, blockHead_->recordCount, blockHead_->recordWriteCount);
    UpdateOffsetStatus(sizeof(RecordBlockHead), globalHead_->offsetInfo.blockHeadSize - sizeof(RecordBlockHead));
    return true;
}

bool SanitizerLaunchFinalize::SimdRecordD2H() {
    aclError error = aclrtMemcpyImplOrigin(
        memInfoHost_, blockRemainSize_, memInfo_, blockHead_->writeOffset, ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy record error: %d", error);
        return false;
    }
    UpdateOffsetStatus(blockHead_->writeOffset);
    return true;
}

bool SanitizerLaunchFinalize::SimtErrorD2H() {
    uint64_t allThreadSize = GetAllThreadSize(*globalHead_);
    aclError error = aclrtMemcpyImplOrigin(memInfoHost_, blockRemainSize_,
        memInfoBackUp_ + globalHead_->offsetInfo.simtErrorInfo.offset, allThreadSize, ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy thread error:%d all thread size:%lu", error, allThreadSize);
        return false;
    }
    UpdateOffsetStatus(allThreadSize);
    return true;
}

void SanitizerLaunchFinalize::UpdateShadowMemoryOffset() {
    UpdateOffsetStatus(0, globalHead_->offsetInfo.shadowMemoryInfo.size);
}

bool SanitizerLaunchFinalize::SimtEntryD2H() {
    if (blockHead_->blockInfo.simtEntryUseSize == 0U) {
        return true;
    }
    aclError error = aclrtMemcpyImplOrigin(memInfoHost_, blockRemainSize_,
        memInfoBackUp_ + globalHead_->offsetInfo.simtEntryInfo.offset, blockHead_->blockInfo.simtEntryUseSize,
        ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy simt entry error:%d size:%lu", error, blockHead_->blockInfo.simtEntryUseSize);
        return false;
    }
    UpdateOffsetStatus(blockHead_->blockInfo.simtEntryUseSize);
    return true;
}

void SanitizerLaunchFinalize::ReportBlockInfo() {
    auto checkBlockId = globalHead_->checkParms.checkBlockId;
    uint64_t totalBlockDim = globalHead_->kernelInfo.totalBlockDim;
    DEBUG_LOG("sanitizer finalize with blockDim: %lu, totalBlockDim: %lu", blockDim_, totalBlockDim);
    bool isReportParaBase{};
    ParaBaseRegister paraBase{};
    for (std::size_t blockIdx = 0; blockIdx < totalBlockDim; ++blockIdx) {
        if (globalHead_->supportSimt && blockIdx == 0U) {
            DumpMemoryInfo(memInfoHost_, memInfo_, blockRemainSize_, blockIdx, *globalHead_);
        }
        // 2. copy RecordBlockHead to host
        if (!BlockHeadD2H(blockIdx)) {
            break;
        }

        if (blockHead_->offset == 0 || blockHead_->writeOffset == 0 || blockHead_->recordCount == 0 ||
            blockHead_->recordWriteCount == 0) {
            DEBUG_LOG("no kernel instruction record on subBlock %lu", blockIdx);
        } else {
            // 上报ParaBase地址信息
            ReportParaBase(*blockHead_, paraBase, isReportParaBase);
            // 3. copy simd records in block to host
            if (!SimdRecordD2H()) {
                break;
            }
        }

        // 4. copy simt records, 当device支持simt并且是目标核的情况下才发送simt内存信息，否则会内存越界
        if (globalHead_->supportSimt && (checkBlockId == CHECK_ALL_BLOCK || IsTargetBlockIdx(checkBlockId, blockIdx))) {
            if (!SimtErrorD2H()) {
                break;
            }

            // 5. copy simt entry to host
            if (!SimtEntryD2H()) {
                break;
            }
        }
        // 6. 偏移memInfo到下一个核的开头，并还原指针和变量位置
        auto body = Serialize(sendMemorySize_);
        body.append(static_cast<char *>(static_cast<void *>(memInfoHostBackUp_)), sendMemorySize_);
        SendKernelBlock(body, blockIdx, totalBlockDim);
        ResetOffsetStatus(blockIdx);
    }
    // 算子信息上报结束后，free掉para base地址
    if (isReportParaBase) {
        ReportFree(paraBase.addr, MemInfoSrc::BYPASS, MemInfoDesc::PARA_BASE);
    }
}

void SanitizerLaunchFinalize::Process() {
    memInfoHost_ = new uint8_t[blockSize_];
    memInfoHostBackUp_ = memInfoHost_;
    // 1. copy RecordGlobalHead to host
    if (!GlobalHeadD2H()) {
        return;
    }

    // 2. 保存上一个算子的寄存器状态，以供下一个算子继承
    if (!RegistersD2H()) {
        return;
    }

    // 3. report block Info
    ReportBlockInfo();
}

SanitizerLaunchFinalize::~SanitizerLaunchFinalize() {
    if (memInfoHostBackUp_) {
        delete[] memInfoHostBackUp_;
        memInfoHostBackUp_ = nullptr;
    }
    DevMemManager::Instance().SetMemoryInitFlag(false);
}
