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


#include "InstrReport.h"

#include <iostream>

#include "ConfigManager.h"
#include "DevMemManager.h"
#include "KernelContext.h"
#include "DeviceContext.h"
#include "KernelMatcher.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/HijackedLayerManager.h"
#include "runtime/inject_helpers/FuncContext.h"
#include "runtime/inject_helpers/LaunchContext.h"
#include "runtime/inject_helpers/LaunchManager.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/ArgsManager.h"
#include "utils/InjectLogger.h"
#include "core/PlatformConfig.h"
#include "runtime.h"
#include "runtime/RuntimeOrigin.h"
#include "utils/Protocol.h"
#include "InteractHelper.h"
#include "utils/Numeric.h"
#include "utils/Ustring.h"

namespace {
std::mutex g_instrMutex;

using namespace OnlineShadowMemory;
Register g_registers[C220_A2_A3_MAXCORE_NUM] = {}; // 保存寄存器状态，向下一个算子传递

inline bool InAclNewLaunchCallStack();

inline bool CheckRegIdxValid(int64_t regIdx)
{
    return (regIdx >= 0) && (regIdx <= C220_A2_A3_MAXCORE_NUM - 1);
}

inline bool CheckBlockDimValid(uint64_t blockDim)
{
    if (blockDim == 0) {
        ERROR_LOG("blockDim must be greater than 0");
        return false;
    } else if (blockDim > MAX_BLOCKDIM_NUMS) {
        ERROR_LOG("blockDim set as %lu exceeds then maximum value %lu", blockDim, MAX_BLOCKDIM_NUMS);
        return false;
    }
    return true;
}

inline bool IsC220Arch(DeviceType deviceType)
{
    return deviceType > DeviceType::ASCEND_910B_START && deviceType < DeviceType::ASCEND_910B_END;
}

inline bool IsC310Arch(DeviceType deviceType)
{
    return deviceType > DeviceType::ASCEND_950_START && deviceType < DeviceType::ASCEND_950_END;
}

inline bool HasSubBlocks(DeviceType deviceType)
{
    return IsC220Arch(deviceType) || IsC310Arch(deviceType);
}

inline bool SupportSimt(DeviceType deviceType)
{
    return IsC310Arch(deviceType);
}

inline uint64_t GetAllThreadSize(RecordGlobalHead const &globalHead)
{
    return (globalHead.simtInfo.threadByteSize + sizeof(SimtRecordBlockHead)) * SIMT_THREAD_MAX_SIZE;
}

inline uint64_t GetRecordHeadSize(uint32_t hostMemoryNum)
{
    return CeilByAlignSize<CACHE_LINE_SIZE>(sizeof(RecordBlockHead) + hostMemoryNum * sizeof(HostMemoryInfo));
}

inline bool AssignMemSize(RecordGlobalHead &head, uint64_t blockDim, DeviceType deviceType)
{
    std::lock_guard<std::mutex> guard(g_instrMutex);
    // calculate total block dim
    uint64_t totalBlockDim{};
    uint64_t totalCacheSize{};
    uint64_t singleBlockSize = head.checkParms.cacheSize;
    if (HasSubBlocks(deviceType)) {
        totalBlockDim = C220_MIX_SUB_BLOCKDIM * blockDim;
        const uint8_t vecCubeSum = 2;
        totalCacheSize = head.checkParms.checkBlockId == CHECK_ALL_BLOCK ?
            singleBlockSize * totalBlockDim :
            singleBlockSize * vecCubeSum +
            SINGLE_CHECK_OTHER_BLOCK_CACHE_SIZE * (totalBlockDim - vecCubeSum);
    } else {
        totalBlockDim = blockDim;
        totalCacheSize = head.checkParms.checkBlockId == CHECK_ALL_BLOCK ?
            totalBlockDim * singleBlockSize :
            singleBlockSize + SINGLE_CHECK_OTHER_BLOCK_CACHE_SIZE * (totalBlockDim - 1);
    }
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

// calculate total block dim
inline bool InitGlobalHead(RecordGlobalHead &head, uint64_t blockDim, KernelType kernelType, uint32_t hostMemoryNum)
{
    // 继承上一个算子的寄存器状态
    aclError error = aclrtMemcpyImplOrigin(head.registers, sizeof(Register) * C220_A2_A3_MAXCORE_NUM,
        g_registers, sizeof(Register) * C220_A2_A3_MAXCORE_NUM, ACL_MEMCPY_HOST_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("init memcpy g_registers error: %d", error);
        return false;
    }

    // 检查是否需要双页表地址还原，如是则配置偏移地址
    DeviceType deviceType = GetDeviceTypeBySocVersion(DeviceContext::Local().GetSocVersion());
    if(IsC220Arch(deviceType)) {
        rtError_t err = rtGetL2CacheOffsetOrigin(DeviceContext::Local().GetDeviceId(), &head.kernelInfo.l2CacheOffset);
        if (err != RT_ERROR_NONE) {
            ERROR_LOG("Get L2Cache Offset failed, ret is %d.", err);
            return false;
        }
        DEBUG_LOG("GlobalHead get L2Cache Offset = %lu", head.kernelInfo.l2CacheOffset);
    } else {
        head.kernelInfo.l2CacheOffset = 0;
    }

    SanitizerConfig cliConfig = SanitizerConfigManager::Instance().GetConfig();
    head.checkParms.cacheSize = cliConfig.cacheSize;
    head.checkParms.checkBlockId = cliConfig.checkBlockId;
    head.checkParms.defaultcheck = cliConfig.defaultCheck;
    head.checkParms.racecheck = cliConfig.raceCheck;
    head.checkParms.initcheck = cliConfig.initCheck;
    head.checkParms.synccheck = cliConfig.syncCheck;
    head.checkParms.registerCheck = cliConfig.registerCheck;
    head.kernelInfo.kernelType = kernelType;
    head.kernelInfo.kernelParamNum = KernelContext::Instance().GetKernelParamNum();
    head.supportSimt = SupportSimt(deviceType);
    if (head.supportSimt) {
        // ====|=====|=============
        // SIMD、SIMT、ShadowMemory
        uint32_t ubDynamicSize{};
        if (InAclNewLaunchCallStack()){
            ubDynamicSize = LaunchManager::Local().GetSimtUbDynamicSize();
        } else {
            ubDynamicSize = KernelContext::Instance().GetSimtUbDynamicSize();
        }
        DEBUG_LOG("simtInfo.ubDynamicSize:%u", ubDynamicSize);
        constexpr uint32_t ubDynamicAlignSize = 128;
        head.simtInfo.ubDynamicSize = CeilByAlignSize<ubDynamicAlignSize>(ubDynamicSize);
        uint64_t threadByteSize =
            static_cast<uint64_t>(cliConfig.cacheSize * MB_TO_BYTES * SIMT_CACHE_SIZE_RATIO / SIMT_THREAD_MAX_SIZE);
        uint64_t shadowMemoryByteSize =
            static_cast<uint64_t>(cliConfig.cacheSize * MB_TO_BYTES * SHADOW_MEM_CACHE_SIZE_RATIO);
        if (threadByteSize >= sizeof(SimtRecordBlockHead)) {
            head.simtInfo.threadByteSize = AlignDownSize<CACHE_LINE_SIZE>(threadByteSize - sizeof(SimtRecordBlockHead));
            DEBUG_LOG("simtInfo.threadByteSize=%lu", head.simtInfo.threadByteSize);
        } else {
            uint32_t needCacheSize =
                sizeof(SimtRecordBlockHead) *
                static_cast<uint32_t>(SIMT_THREAD_MAX_SIZE / SIMT_CACHE_SIZE_RATIO / MB_TO_BYTES) + 1;
            ERROR_LOG("cache-size:%u is too small, need cache-size=%u", head.checkParms.cacheSize, needCacheSize);
            return false;
        }
        uint64_t simtOffset = static_cast<uint64_t>(cliConfig.cacheSize * MB_TO_BYTES *
            (1 - SIMT_CACHE_SIZE_RATIO - SHADOW_MEM_CACHE_SIZE_RATIO));
        head.simtInfo.offset = AlignDownSize<CACHE_LINE_SIZE>(simtOffset) + GetRecordHeadSize(hostMemoryNum);
        DEBUG_LOG("simtInfo.offset=%lu", head.simtInfo.offset);
    
        if (shadowMemoryByteSize < SHADOW_MEM_MIN_BYTE_SIZE) {
            WARN_LOG("overlapping detection between threads is disabled. "
                "cache-size:%u is too small, need cache-size >= %lu",
                head.checkParms.cacheSize,
                static_cast<uint64_t>(SHADOW_MEM_MIN_BYTE_SIZE / MB_TO_BYTES / SHADOW_MEM_CACHE_SIZE_RATIO));
            head.simtInfo.shadowMemoryOffset = 0U;
            head.simtInfo.shadowMemoryByteSize = 0U;
        } else {
            head.simtInfo.shadowMemoryOffset = AlignDownSize<CACHE_LINE_SIZE>(head.simtInfo.offset +
                GetAllThreadSize(head) + CACHE_LINE_SIZE - 1);
            head.simtInfo.shadowMemoryByteSize = shadowMemoryByteSize;
            DEBUG_LOG("simtInfo shadowMemoryOffset=%lu shadowMemoryByteSize=%lu",
                head.simtInfo.shadowMemoryOffset, head.simtInfo.shadowMemoryByteSize);
        }
    }
    return AssignMemSize(head, blockDim, deviceType);
}

inline bool IsTargetBlockIdx(int16_t checkBlockId, uint64_t blockIdx)
{
    DeviceType deviceType = GetDeviceTypeBySocVersion(DeviceContext::Local().GetSocVersion());
    if (HasSubBlocks(deviceType)) {
        uint64_t vecTargetBlockIdx = checkBlockId / C220_VEC_SUB_BLOCKDIM * C220_MIX_SUB_BLOCKDIM +
            checkBlockId % C220_VEC_SUB_BLOCKDIM;
        uint64_t cubeTargetBlockIdx = checkBlockId * C220_MIX_SUB_BLOCKDIM + C220_VEC_SUB_BLOCKDIM;
        return blockIdx == vecTargetBlockIdx || blockIdx == cubeTargetBlockIdx;
    } else {
        return blockIdx == static_cast<uint64_t>(checkBlockId);
    }
}

inline void UpdateBlockHeadOffset(CheckParmsInfo &checkParms, uint64_t blockIdx, uint32_t hostMemoryNum,
    uint64_t &blockHeadOffset)
{
    auto checkBlockId = checkParms.checkBlockId;
    auto cacheSize = checkParms.cacheSize;
    if (checkBlockId == CHECK_ALL_BLOCK || IsTargetBlockIdx(checkBlockId, blockIdx)) {
        blockHeadOffset += cacheSize * MB_TO_BYTES + GetRecordHeadSize(hostMemoryNum);
    } else {
        blockHeadOffset += SINGLE_CHECK_OTHER_BLOCK_CACHE_SIZE * MB_TO_BYTES + GetRecordHeadSize(hostMemoryNum);
    }
}

void SendKernelBlock(const std::string &body, int curBlkIdx, int totalBlkIdx)
{
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

/// copy simd records in block to host
bool CopySimdRecordToHost(uint8_t *memInfoHost, uint8_t *memInfo, RecordGlobalHead const &globalHead,
                          RecordBlockHead const &head, uint64_t blockSize)
{
    (void)globalHead;

    aclError error = aclrtMemcpyImplOrigin(memInfoHost, blockSize - sizeof(RecordGlobalHead) - sizeof(RecordBlockHead),
        memInfo, head.writeOffset, ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy record error: %d", error);
        return false;
    }
    return true;
}

/// copy all simt thread records in block to host
bool CopySimtRecordToHost(uint8_t *memInfoHost, uint8_t *memInfo, RecordGlobalHead const &globalHead,
                          RecordBlockHead const &head, uint64_t blockSize)
{
    uint64_t allThreadSize = GetAllThreadSize(globalHead);
    aclError error = aclrtMemcpyImplOrigin(memInfoHost,
        blockSize - sizeof(RecordGlobalHead) - sizeof(RecordBlockHead) - head.writeOffset,
        memInfo + globalHead.simtInfo.offset, allThreadSize, ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy thread error:%d all thread size:%lu", error, allThreadSize);
        return false;
    }
    return true;
}

uint8_t CountOneBits(uint64_t number)
{
    uint8_t count = 0;
    while (number != 0) {
        number &= number - 1;
        ++count;
    }
    return count;
}

/// 把threadId按三维展开为(x,y,z)
inline void DecomposeThreadId(uint16_t threadId, RecordBlockHead const &head, SimtThreadLocation &threadLoc)
{
    uint16_t threadXDim = head.blockInfo.threadXDim;
    uint16_t threadYDim = head.blockInfo.threadYDim;
    if (threadXDim == 0 || threadYDim == 0) {
        ERROR_LOG("threadXDim or threadYDim equal 0 error in blockId:%d", head.blockInfo.blockId);
        return;
    }
    threadLoc.idX = threadId % threadXDim;
    threadLoc.idY = (threadId % (threadXDim * threadYDim)) / threadXDim;
    threadLoc.idZ = threadId / (threadXDim * threadYDim);
}

void MergeSmRecord(std::vector<ShadowMemoryRecord> &records, uint64_t status, uint64_t addr, RecordBlockHead const &head)
{
    ShadowMemoryRecord record{};
    record.addr = addr;
    record.size = 1;
    record.space = AddressSpace::GM;
    record.opType = MemOpType::STORE;
    uint16_t threadId = status & THREAD_ID_MASK;
    DecomposeThreadId(threadId, head, record.threadLoc);
    record.location.pc = (status >> PC_START_BIT) & PC_MASK;
    record.location.blockId = head.blockInfo.blockId;
    if (records.size() == 0) {
        records.emplace_back(record);
        return;
    }
    auto &back = records.back();
    if (back.location == record.location && back.opType == record.opType && back.space == record.space &&
        back.threadLoc == record.threadLoc) {
        if (back.addr + back.size == record.addr) {
            back.size += record.size;
            return;
        }
    }
    records.emplace_back(record);
}

inline void ParseSmL2Table(uint64_t *l2TblPtr, size_t l0Idx, size_t l1Idx, RecordBlockHead const &head,
                           std::vector<ShadowMemoryRecord> &records)
{
    uint8_t l1OneBits = CountOneBits(ONLINE_LOCAL_MEM_MASK);
    uint8_t l2OneBits = CountOneBits(ONLINE_ONE_SM_STAND_FOR_BYTE - 1);
    for (size_t l2Idx = 0; l2Idx < ONLINE_ONE_SM_STAND_FOR_BYTE; ++l2Idx) {
        uint64_t status = l2TblPtr[l2Idx];
        MemoryByteStatus memStatus = static_cast<MemoryByteStatus>((status >> MEMORY_STATUS_START_BIT) &
            MEMORY_STATUS_MASK);
        OnlineMemoryType memType = static_cast<OnlineMemoryType>((status >> MEMORY_TYPE_START_BIT) &
            MEMORY_TYPE_MASK);
        if ((memStatus == MemoryByteStatus::WRITE || memStatus == MemoryByteStatus::RACE) &&
            memType == OnlineMemoryType::GM) {
            uint64_t addr = (l0Idx << l1OneBits) | (l1Idx << l2OneBits) | l2Idx;
            MergeSmRecord(records, status, addr, head);
        }
    }
}

inline void ParseSmL1Table(uint8_t *memInfoHost, uint64_t smBaseAddr, RecordBlockHead const &head,
                           size_t l0Idx, std::vector<ShadowMemoryRecord> &records)
{
    uint64_t l1TblNum = (ONLINE_LOCAL_MEM_MASK + ONLINE_ONE_SM_STAND_FOR_BYTE - 1U) / ONLINE_ONE_SM_STAND_FOR_BYTE;
    auto l0TblPtr = reinterpret_cast<uint64_t *>(memInfoHost + sizeof(ShadowMemoryHeapHead));
    uint64_t l1TblOffset = l0TblPtr[l0Idx] - smBaseAddr;
    auto l1TblPtr = reinterpret_cast<uint64_t *>(memInfoHost + l1TblOffset);
    for (size_t l1Idx = 0; l1Idx < l1TblNum; ++l1Idx) {
        if ((l1TblPtr[l1Idx] == 0x0) ||
            (l1TblPtr[l1Idx] == static_cast<uint64_t>(OnlineSmAddrStatus::UNALLOCATABLE)) ||
            (l1TblPtr[l1Idx] == static_cast<uint64_t>(OnlineSmAddrStatus::LOCKED_BY_OTHER_THREADS))) {
            continue;
        }
        uint64_t l2TblOffset = l1TblPtr[l1Idx] - smBaseAddr;
        auto l2TblPtr = reinterpret_cast<uint64_t *>(memInfoHost + l2TblOffset);
        ParseSmL2Table(l2TblPtr, l0Idx, l1Idx, head, records);
    }
}

inline void ParseSmTable(uint8_t *memInfoHost, uint64_t smBaseAddr, RecordBlockHead const &head,
                         std::vector<ShadowMemoryRecord> &records)
{
    uint64_t l0TblNum = (ONLINE_GLOBAL_MEM_MASK + ONLINE_LOCAL_MEM_MASK - 1U) / ONLINE_LOCAL_MEM_MASK;
    auto l0TblPtr = reinterpret_cast<uint64_t *>(memInfoHost + sizeof(ShadowMemoryHeapHead));
    for (size_t l0Idx = 0; l0Idx < l0TblNum; ++l0Idx) {
        if ((l0TblPtr[l0Idx] == 0x0) ||
            (l0TblPtr[l0Idx] == static_cast<uint64_t>(OnlineSmAddrStatus::UNALLOCATABLE)) ||
            (l0TblPtr[l0Idx] == static_cast<uint64_t>(OnlineSmAddrStatus::LOCKED_BY_OTHER_THREADS))) {
            continue;
        }
        ParseSmL1Table(memInfoHost, smBaseAddr, head, l0Idx, records);
    }
}

/// copy all shadow memory records in block to host
uint64_t CopyShoadowMemoryToHost(uint8_t *memInfoHost, uint8_t *memInfo, RecordGlobalHead const &globalHead,
RecordBlockHead const &head, uint64_t blockSize)
{
    uint64_t blockRemainSize = blockSize - sizeof(RecordGlobalHead) - sizeof(RecordBlockHead) - head.writeOffset -
    GetAllThreadSize(globalHead);
    if (aclrtMemcpyImplOrigin(memInfoHost, blockRemainSize, memInfo + globalHead.simtInfo.shadowMemoryOffset,
        globalHead.simtInfo.shadowMemoryByteSize, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy shadow memory error");
        return {};
    }
    uint64_t smBaseAddr = reinterpret_cast<uint64_t>(memInfo) + globalHead.simtInfo.shadowMemoryOffset;
    std::vector<ShadowMemoryRecord> records;
    ParseSmTable(memInfoHost, smBaseAddr, head, records);

    ShadowMemoryRecordHead shadowMemoryHead{};
    shadowMemoryHead.recordCount = records.size();
    if (aclrtMemcpyImplOrigin(memInfoHost, blockRemainSize, &shadowMemoryHead, sizeof(shadowMemoryHead),
        ACL_MEMCPY_HOST_TO_HOST) != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy shadow memory record head error");
        return {};
    }
    if (records.size() == 0U) { return sizeof(shadowMemoryHead); }
    if (aclrtMemcpyImplOrigin(memInfoHost + sizeof(shadowMemoryHead), blockRemainSize - sizeof(shadowMemoryHead),
        records.data(), records.size() * sizeof(ShadowMemoryRecord), ACL_MEMCPY_HOST_TO_HOST) != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy shadow memory record error");
        return sizeof(shadowMemoryHead);
    }
    return sizeof(shadowMemoryHead) + records.size() * sizeof(ShadowMemoryRecord);
}

void ReportParaBase(RecordBlockHead const &head, ParaBaseRegister &paraBase, bool &isReportParaBase)
{
    const auto &paraBaseInfo = head.paraBase;
    if (!isReportParaBase && paraBaseInfo.addr != ILLEGAL_ADDR && paraBaseInfo.size != 0) {
        ReportMalloc(paraBaseInfo.addr, paraBaseInfo.size, MemInfoSrc::BYPASS, MemInfoDesc::PARA_BASE);
        ReportMemset(paraBaseInfo.addr, paraBaseInfo.size, MemInfoSrc::BYPASS, MemInfoDesc::PARA_BASE);
        paraBase = paraBaseInfo;
        isReportParaBase = true;
    }
}

/// 落盘在线检测对应的malloc内存，便于后续定位问题
void DumpMemoryInfo(uint8_t *memInfoHost, uint8_t *memInfo, uint64_t blockSize, uint64_t blockIdx)
{
    aclError error = aclrtMemcpyImplOrigin(memInfoHost + sizeof(RecordGlobalHead),
        blockSize - sizeof(RecordGlobalHead), memInfo + sizeof(RecordGlobalHead),
        sizeof(RecordBlockHead), ACL_MEMCPY_DEVICE_TO_HOST);
    auto *blockHead = reinterpret_cast<RecordBlockHead *>(memInfoHost + sizeof(RecordGlobalHead));
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy hostMemInfo error: %d", error);
        return;
    }
    if (blockHead->hostMemoryNum == 0U) { return; }
    uint64_t recordHeadSize = GetRecordHeadSize(blockHead->hostMemoryNum);
    if (sizeof(RecordGlobalHead) + recordHeadSize > blockSize) {
        DEBUG_LOG("too much host memory");
        return;
    };
    blockHead->hostMemoryInfoPtr = reinterpret_cast<HostMemoryInfo *>(blockHead + 1);
    uint64_t allHeadSize = sizeof(RecordGlobalHead) + sizeof(RecordBlockHead);
    error = aclrtMemcpyImplOrigin(memInfoHost + allHeadSize, blockSize - allHeadSize, memInfo + allHeadSize,
        recordHeadSize - sizeof(RecordBlockHead), ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy hostMemInfo error: %d", error);
        return;
    }
    for (size_t i = 0; i < blockHead->hostMemoryNum; ++i) {
        auto memory = blockHead->hostMemoryInfoPtr[i];
        DEBUG_LOG("online memcheck in block:%lu addr:0x%lx size:%lu", blockIdx, memory.addr, memory.size);
    }
}

std::vector<HostMemoryInfo> CopyExtraMallocToHostMemory()
{
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

void ReportMemInfo(uint8_t *memInfoHost, uint8_t *memInfo, uint64_t blockSize, uint64_t blockDim)
{
    // 1. copy RecordGlobalHead to host
    aclError error = aclrtMemcpyImplOrigin(memInfoHost, blockSize, memInfo,
                                           sizeof(RecordGlobalHead), ACL_MEMCPY_DEVICE_TO_HOST);
    auto *globalHead = reinterpret_cast<RecordGlobalHead *>(memInfoHost);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy RecordGlobalHead error: %d", error);
        return;
    }

    // 保存上一个算子的寄存器状态，以供下一个算子继承
    error = aclrtMemcpyImplOrigin(g_registers, sizeof(Register) * C220_A2_A3_MAXCORE_NUM,
        globalHead->registers, sizeof(Register) * C220_A2_A3_MAXCORE_NUM, ACL_MEMCPY_HOST_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("finalize memcpy g_registers error: %d", error);
        return;
    }

    auto checkBlockId = globalHead->checkParms.checkBlockId;
    uint64_t totalBlockDim = globalHead->kernelInfo.totalBlockDim;
    DEBUG_LOG("sanitizer finalize with blockDim: %lu, totalBlockDim: %lu", blockDim, totalBlockDim);
    uint64_t blockHeadOffset{};
    bool isReportParaBase{};
    ParaBaseRegister paraBase{};
    uint64_t hostRecordOffset = sizeof(RecordGlobalHead) + sizeof(RecordBlockHead);
    for (std::size_t blockIdx = 0; blockIdx < totalBlockDim; ++blockIdx) {
        if (globalHead->supportSimt) { DumpMemoryInfo(memInfoHost, memInfo + blockHeadOffset, blockSize, blockIdx); }
        // 2. copy RecordBlockHead to host
        error = aclrtMemcpyImplOrigin(memInfoHost + sizeof(RecordGlobalHead), blockSize - sizeof(RecordGlobalHead),
                                      memInfo + sizeof(RecordGlobalHead) + blockHeadOffset,
                                      sizeof(RecordBlockHead), ACL_MEMCPY_DEVICE_TO_HOST);
        auto *head = reinterpret_cast<RecordBlockHead *>(memInfoHost + sizeof(RecordGlobalHead));
        if (error != ACL_ERROR_NONE) {
            ERROR_LOG("finalize memcpy RecordBlockHead error: %d", error);
            break;
        }

        DEBUG_LOG("get recordHead from subBlock %lu, offset:%lu, writeOffset:%lu recordCount:%lu, recordWriteCount:%lu",
                  blockIdx, head->offset, head->writeOffset, head->recordCount, head->recordWriteCount);
        if (head->offset == 0 || head->writeOffset == 0 || head->recordCount == 0 || head->recordWriteCount == 0) {
            DEBUG_LOG("no kernel instruction record on subBlock %lu", blockIdx);
        } else {
            /// 上报ParaBase地址信息
            ReportParaBase(*head, paraBase, isReportParaBase);

            // 3. copy simd records in block to host
            if (!CopySimdRecordToHost(memInfoHost + hostRecordOffset, memInfo + sizeof(RecordGlobalHead) +
                GetRecordHeadSize(head->hostMemoryNum) + blockHeadOffset, *globalHead, *head, blockSize)) { break; }
        }

        /// 4. copy simt records, 当device支持simt并且是目标核的情况下才发送simt内存信息，否则会内存越界
        uint64_t simtMemorySize{};
        if (globalHead->supportSimt && (checkBlockId == CHECK_ALL_BLOCK || IsTargetBlockIdx(checkBlockId, blockIdx))) {
            if (!CopySimtRecordToHost(memInfoHost + hostRecordOffset + head->writeOffset,
                memInfo + sizeof(RecordGlobalHead) + blockHeadOffset, *globalHead, *head, blockSize)) { break; }
            simtMemorySize += GetAllThreadSize(*globalHead);
            uint64_t smSize = CopyShoadowMemoryToHost(memInfoHost + hostRecordOffset + head->writeOffset +
                simtMemorySize, memInfo + sizeof(RecordGlobalHead) + blockHeadOffset, *globalHead, *head, blockSize);
            if (smSize == 0U) { break; }
            simtMemorySize += smSize;
        }
        UpdateBlockHeadOffset(globalHead->checkParms, blockIdx, head->hostMemoryNum, blockHeadOffset);
        uint64_t memSize = hostRecordOffset + head->writeOffset + simtMemorySize;
        auto body = Serialize(memSize);
        body.append(static_cast<char*>(static_cast<void*>(memInfoHost)), memSize);
        SendKernelBlock(body, blockIdx, totalBlockDim);
    }
    /// 算子信息上报结束后，free掉para base地址
    if (isReportParaBase) { ReportFree(paraBase.addr, MemInfoSrc::BYPASS, MemInfoDesc::PARA_BASE); }
}

bool InitSimtShadowMemoryHead(const RecordGlobalHead &recordGlobalHead, uint8_t *&memInfo, uint64_t blockHeadOffset)
{
    uint64_t shadowMemorySize = recordGlobalHead.simtInfo.shadowMemoryByteSize;
    uint64_t shadowMemoryOffset = recordGlobalHead.simtInfo.shadowMemoryOffset;
    uint8_t *shadowMemoryHeadAddr = memInfo + sizeof(RecordGlobalHead) + blockHeadOffset + shadowMemoryOffset;

    aclError error = aclrtMemsetImplOrigin(shadowMemoryHeadAddr, shadowMemorySize, 0x00, shadowMemorySize);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("init memset 0 to shadow memory heap error: %d", error);
        return false;
    }

    ShadowMemoryHeapHead smHeapHead;
    smHeapHead.startAddr = (uint64_t)shadowMemoryHeadAddr + sizeof(ShadowMemoryHeapHead);
    smHeapHead.size = shadowMemorySize > sizeof(ShadowMemoryHeapHead) ?
        shadowMemorySize - sizeof(ShadowMemoryHeapHead) : 0U;
    // 预分配shadow memory L0级表空间，每个节点占8字节
    smHeapHead.current = smHeapHead.startAddr +
        ((ONLINE_GLOBAL_MEM_MASK + ONLINE_LOCAL_MEM_MASK - 1U) / ONLINE_LOCAL_MEM_MASK) * sizeof(uint64_t);
    smHeapHead.lock = 0U;

    DEBUG_LOG("ShadowMemoryHeapHead on 0x%lx startAddr=0x%lx size=0x%lx current=0x%lx lock=0x%lx",
        reinterpret_cast<uint64_t>(shadowMemoryHeadAddr),
        smHeapHead.startAddr, smHeapHead.size, smHeapHead.current, smHeapHead.lock);

    error = aclrtMemcpyImplOrigin(shadowMemoryHeadAddr,
        sizeof(ShadowMemoryHeapHead), static_cast<const void*>(&smHeapHead),
        sizeof(ShadowMemoryHeapHead), ACL_MEMCPY_HOST_TO_DEVICE);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("init shadow memory heap head error: %d", error);
        return false;
    }

    return true;
}

bool InitSimtBlockInfo(RecordGlobalHead &recordGlobalHead, uint8_t *&memInfo, size_t blockIdx, uint64_t blockHeadOffset)
{
    if (!recordGlobalHead.supportSimt) {
        return true;
    }
    std::vector<HostMemoryInfo> hostMems = CopyExtraMallocToHostMemory();
    // 3. 初始化每个核RecordBlockHead中的hostMemoryInfoPtr
    aclError error;
    if (hostMems.size() != 0U) {
        error = aclrtMemcpyImplOrigin(memInfo + sizeof(RecordGlobalHead) + sizeof(RecordBlockHead) +
            blockHeadOffset, hostMems.size() * sizeof(HostMemoryInfo), static_cast<const void*>(hostMems.data()),
            hostMems.size() * sizeof(HostMemoryInfo), ACL_MEMCPY_HOST_TO_DEVICE);
        if (error != ACL_ERROR_NONE) {
            ERROR_LOG("init block: %ld host malloc mem to record block head error: %d", blockIdx, error);
            return false;
        }
    }

    /// 4. 初始化simtRecordHead
    int16_t checkBlockId = recordGlobalHead.checkParms.checkBlockId;
    if (checkBlockId == CHECK_ALL_BLOCK || IsTargetBlockIdx(checkBlockId, blockIdx)) {
        uint64_t allThreadSize = GetAllThreadSize(recordGlobalHead);
        error = aclrtMemsetImplOrigin(memInfo + sizeof(RecordGlobalHead) + blockHeadOffset +
            recordGlobalHead.simtInfo.offset, allThreadSize, 0x00, allThreadSize);
        if (error != ACL_ERROR_NONE) {
            ERROR_LOG("init memset simt record block head error: %d", error);
            return false;
        }

        // 初始化 shadow memory
        if (recordGlobalHead.simtInfo.shadowMemoryByteSize == 0U) {
            DEBUG_LOG("shadow memory disabled for block %lu", blockIdx);
        } else {
            if (!InitSimtShadowMemoryHead(recordGlobalHead, memInfo, blockHeadOffset)) {
                ERROR_LOG("init shadow memory head error for block: %lu", blockIdx);
                return false;
            }
        }
    }

    return true;
}

inline bool InAclNewLaunchCallStack()
{
    return
        HijackedLayerManager::Instance().InCallStack("aclrtLaunchKernelImpl") ||
        HijackedLayerManager::Instance().InCallStack("aclrtLaunchKernelWithConfigImpl") ||
        HijackedLayerManager::Instance().InCallStack("aclrtLaunchKernelWithHostArgsImpl");
}

bool InitRecordHeaders(RecordGlobalHead &recordGlobalHead, uint8_t *memInfo, uint32_t hostMemoryNum)
{
    aclError error;
    auto &devMemManager = DevMemManager::Instance();

    // 1. 初始化 RecordGlobalHead
    error = aclrtMemcpyImplOrigin(memInfo, sizeof(recordGlobalHead),
                                  static_cast<const void*>(&recordGlobalHead),
                                  sizeof(recordGlobalHead), ACL_MEMCPY_HOST_TO_DEVICE);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("init memcpy record global head error: %d", error);
        devMemManager.Free();
        return false;
    }

    RecordBlockHead recordBlockHead{};
    if (InAclNewLaunchCallStack()) {
 	    recordBlockHead.paraBase.size = ArgsManager::Instance().GetArgsSize();
 	} else {
 	    recordBlockHead.paraBase.size = KernelContext::Instance().GetArgsSize();
 	}
    recordBlockHead.hostMemoryNum = hostMemoryNum;
    uint64_t blockHeadOffset = 0UL;
    std::vector<HostMemoryInfo> hostMems = CopyExtraMallocToHostMemory();

    for (size_t blockIdx = 0; blockIdx < recordGlobalHead.kernelInfo.totalBlockDim; ++blockIdx) {
        // 2. 初始化每个核的 RecordBlockHead
        DEBUG_LOG("blockHeadOffset=%lu", blockHeadOffset);
        recordBlockHead.hostMemoryInfoPtr = hostMemoryNum > 0 ?
            reinterpret_cast<HostMemoryInfo *>(memInfo + sizeof(RecordGlobalHead) + blockHeadOffset +
                sizeof(RecordBlockHead)) : nullptr;
        error = aclrtMemcpyImplOrigin(memInfo + sizeof(RecordGlobalHead) + blockHeadOffset,
            sizeof(RecordBlockHead), static_cast<const void*>(&recordBlockHead),
            sizeof(RecordBlockHead), ACL_MEMCPY_HOST_TO_DEVICE);
        if (error != ACL_ERROR_NONE) {
            ERROR_LOG("init memcpy simd record block head error: %d", error);
            devMemManager.Free();
            return false;
        }

        if (!InitSimtBlockInfo(recordGlobalHead, memInfo, blockIdx, blockHeadOffset)) {
            devMemManager.Free();
            return false;
        }

        UpdateBlockHeadOffset(recordGlobalHead.checkParms, blockIdx, hostMemoryNum, blockHeadOffset);
    }
    return true;
}

} // namespace [Dummy]
 
std::string GetArchName(KernelType kernelType, const std::string &socVersion)
{
    if (socVersion.find("Ascend910B") != std::string::npos ||
        socVersion.find("Ascend910_93") != std::string::npos) {
        switch (kernelType) {
            case KernelType::AIVEC:
                return "dav-c220-vec";
            case KernelType::AICUBE:
                return "dav-c220-cube";
            case KernelType::MIX:
                return "dav-c220";
            default:
                return "";
        }
    }

    if (socVersion.find("Ascend310P") != std::string::npos) {
        switch (kernelType) {
            case KernelType::AIVEC:
                return "dav-m200-vec";
            case KernelType::MIX:
                return "dav-m200";
            default:
                return "";
        }
    }

    if (socVersion.find("Ascend950") != std::string::npos) {
        switch (kernelType) {
            case KernelType::AIVEC:
                return "dav-c310-vec";
            case KernelType::AICUBE:
                return "dav-c310-cube";
            case KernelType::MIX:
                return "dav-c310";
            default:
                return "";
        }
    }
    return "";
}

std::string GetCurrentArchName()
{
    KernelType kernelType = GetCurrentKernelType();
    std::string socVersion = DeviceContext::Local().GetSocVersion();
    return GetArchName(kernelType, socVersion);
}

uint8_t *__sanitizer_init(uint64_t blockDim)
{
    if (!CheckBlockDimValid(blockDim)) {
        return nullptr;
    }

    KernelType kernelType{};
    if (InAclNewLaunchCallStack()) {
        LaunchContextSP launchCtx = LaunchManager::Local().GetLastContext();
        if (launchCtx) {
            const auto &funcContext = launchCtx->GetFuncContext();
            kernelType = funcContext->GetKernelType();
        }
    } else {
        kernelType = GetCurrentKernelType();
    }

    uint32_t hostMemoryNum = MemoryManage::Instance().GetMallocCount();
    RecordGlobalHead recordGlobalHead{};
    if (!InitGlobalHead(recordGlobalHead, blockDim, kernelType, hostMemoryNum)) {
        return nullptr;
    }

    uint64_t totalBlockDim = recordGlobalHead.kernelInfo.totalBlockDim;
    DEBUG_LOG("sanitizer init with blockDim: %lu, totalBlockDim: %lu", blockDim, totalBlockDim);
    auto &devMemManager = DevMemManager::Instance();
    bool isSkipKernel = devMemManager.IsSkipKernel();
    // 对于不需要检测的kernel，申请1M即可，优化整体检测耗时
    uint64_t memSize = isSkipKernel ? MB_TO_BYTES : recordGlobalHead.kernelInfo.totalCacheSize * MB_TO_BYTES +
        sizeof(RecordGlobalHead) + totalBlockDim * GetRecordHeadSize(hostMemoryNum);
    void *memPtr = nullptr;
    aclError error = devMemManager.MallocMemory(&memPtr, memSize);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("init malloc memInfo error: %d", error);
        return nullptr;
    }

    auto *memInfo = static_cast<uint8_t*>(memPtr);

    if (isSkipKernel) { return memInfo; }

    if (!InitRecordHeaders(recordGlobalHead, memInfo, hostMemoryNum)) {
        return nullptr;
    }

    if (devMemManager.IsMemoryInit()) {
        DEBUG_LOG("memInfo is already initialized, skip init");
        return memInfo;
    }

    devMemManager.SetMemoryInitFlag(true);
    return memInfo;
}

void __sanitizer_finalize(uint8_t *memInfo, uint64_t blockDim)
{
    auto &devMemManager = DevMemManager::Instance();
    if (!devMemManager.IsMemoryInit() || !CheckBlockDimValid(blockDim)) {return; }

    uint64_t blockSize = SanitizerConfigManager::Instance().GetConfig().cacheSize * MB_TO_BYTES +
        sizeof(RecordGlobalHead) + sizeof(RecordBlockHead);
    auto *memInfoHost = new uint8_t[blockSize];
    ReportMemInfo(memInfoHost, memInfo, blockSize, blockDim);

    delete[] memInfoHost;
    memInfoHost = nullptr;
    devMemManager.SetMemoryInitFlag(false);
}

KernelType GetCurrentKernelType()
{
    uint64_t launchId = KernelContext::Instance().GetLaunchId();
    uint64_t registerId = KernelContext::Instance().GetRegisterId(launchId);
    KernelType kernelType {};
    KernelContext::RegisterEvent registerEvent;
    KernelContext::LaunchEvent launchEvnet{};
    if (KernelContext::Instance().GetRegisterEvent(registerId, registerEvent) &&
        KernelContext::Instance().GetLaunchEvent(launchId, launchEvnet)) {
        kernelType = GetKernelType(registerEvent, launchEvnet);
    }
    return kernelType;
}

KernelType GetKernelType(KernelContext::RegisterEvent const &registerEvent,
                         KernelContext::LaunchEvent const &launchEvent)
{
    auto kernelType = MagicToKernelType(registerEvent.bin.magic);
    if (kernelType != KernelType::MIX) {
        return kernelType;
    }

    std::string kernelName = launchEvent.kernelName;
    RemoveSuffix(kernelName, { MIX_AIC_SUFFIX, MIX_AIV_SUFFIX });
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

bool SkipSanitizer(std::string const &kernelName)
{
    // 使用工具侧传过来的 KernelName 判断是否需要跳过当前的 kernel 检测
    SanitizerConfig const &sanitizerConfig = SanitizerConfigManager::Instance().GetConfig();
    KernelMatcher::Config matchConfig {};
    matchConfig.kernelName = sanitizerConfig.kernelName;
    bool skipSanitizer = !KernelMatcher(matchConfig).Match(0, kernelName);
    if (skipSanitizer) {
        DEBUG_LOG("skip sanitizer for kernel name: %s", kernelName.c_str());
    } else {
        DEBUG_LOG("do sanitizer for kernel name: %s", kernelName.c_str());
    }
    return skipSanitizer;
}

std::string GetTargetArchName(const FuncContextSP &funcCtx)
{
    auto kernelType = funcCtx->GetKernelType();
    const auto &socVersion = DeviceContext::Local().GetSocVersion();
    return GetArchName(kernelType, socVersion);
}

void ReportKernelSummary(uint64_t launchId)
{
    KernelContext::LaunchEvent event;
    if (!KernelContext::Instance().GetLaunchEvent(launchId, event)) {
        return;
    }
    uint64_t registerId = KernelContext::Instance().GetRegisterId(launchId);
    KernelContext::RegisterEvent registerEvent;
    if (!KernelContext::Instance().GetRegisterEvent(registerId, registerEvent)) {
        return;
    }
    KernelSummary kernelSummary;
    kernelSummary.pcStartAddr = event.pcStartAddr;
    kernelSummary.blockDim = event.blockDim;
    kernelSummary.kernelType = GetCurrentKernelType();
    kernelSummary.isKernelWithDBI = registerEvent.isKernelWithDBI;
    kernelSummary.hasDebugLine = registerEvent.hasDebugLine;
    std::size_t length = event.kernelName.copy(kernelSummary.kernelName, sizeof(kernelSummary.kernelName) - 1);
    kernelSummary.kernelName[length] = '\0';

    PacketHead head = { PacketType::KERNEL_SUMMARY };
    LocalDevice::Local().Notify(Serialize(head, kernelSummary));
}

void ReportKernelSummary(LaunchContextSP launchCtx)
{
    if (launchCtx == nullptr) {
        return;
    }

    FuncContextSP funcCtx = launchCtx->GetDBIFuncCtx();
    if (funcCtx == nullptr) {
        funcCtx = launchCtx->GetFuncContext();
    }
    RegisterContextSP regCtx = funcCtx->GetRegisterContext();

    KernelSummary kernelSummary;
    kernelSummary.kernelType = funcCtx->GetKernelType();
    kernelSummary.pcStartAddr = funcCtx->GetStartPC();
    kernelSummary.blockDim = launchCtx->GetLaunchParam().blockDim;
    kernelSummary.isKernelWithDBI = !regCtx->HasStaticStub();
    kernelSummary.hasDebugLine = regCtx->HasDebugLine();
    std::size_t length = funcCtx->GetKernelName().copy(kernelSummary.kernelName, sizeof(kernelSummary.kernelName) - 1);
    kernelSummary.kernelName[length] = '\0';

    PacketHead head = { PacketType::KERNEL_SUMMARY };
    LocalDevice::Local().Notify(Serialize(head, kernelSummary));
}
