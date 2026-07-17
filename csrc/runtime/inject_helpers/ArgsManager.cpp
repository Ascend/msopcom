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

#include "ArgsManager.h"
#include "ArgsHandleContext.h"
#include "ArgsRawContext.h"
#include "KernelMetaDataParser.h"
#include "utils/InjectLogger.h"
#include "hccl.h"
#include "acl_rt_impl/AscendclImplOrigin.h"

using namespace std;
using Defer = std::shared_ptr<void>;

namespace {
constexpr uint64_t U64_BYTE_SIZE = 8U;
constexpr uint64_t META_MAGIC_NUM = 0xA5A5A5A500000000;

AdumpInfo GetAdumpInfoFromTilingData(const vector<uint8_t> &tilingData, const OpMemInfo &memInfo)
{
    AdumpInfo info;
    const uint8_t *tilingBuffData = tilingData.data();
    if (!memInfo.isTik) {
        if (memInfo.tilingDataSize < U64_BYTE_SIZE) {
            ERROR_LOG("Get adump context failed, tiling data size is lower than U64_BYTE_SIZE");
            return {};
        }
        uint64_t atomicIndex = *(reinterpret_cast<const uint64_t*>(tilingBuffData + memInfo.tilingDataSize - U64_BYTE_SIZE));
        if (ArgsManager::Instance().GetAdumpInfo(atomicIndex, info)) {
            DEBUG_LOG("Get adump info from ascendc op");
            return info;
        }
        // 以前这里就没有返回，这里保持一致，应该是因为几乎不会走到这里
    }
    // tik算子的解析
    uint8_t count = 0U;
    // atomic_index的前4个字节由A5组成
    for (size_t i = 4; i < memInfo.tilingDataSize; i++) {
        // 5A是魔术字前缀的一部分
        if (*(tilingBuffData + i) == 0xA5) {
            count++;
        } else {
            count = 0;
        }
        // 魔术字的前缀由连续4个5A构成，一共要取8个字节，仅凭前7个不可能找到魔术字
        if (count != 4 || i < 7) {
            continue;
        }
        // 魔术字小端保存，类似00,00,00,00,A5,A5,A5,A5，从前往后找，找到的index是a+7，需要取值的index是a
        auto value = *(reinterpret_cast<const uint32_t *>((tilingBuffData + (i - 7))));
        auto atomicIndex = META_MAGIC_NUM | value;
        if (ArgsManager::Instance().GetAdumpInfo(atomicIndex, info)) {
            DEBUG_LOG("Get adump info from tbe op");
            return info;
        }
        count -= 1;
    }
    DEBUG_LOG("Get empty adump info");
    return {};
}

// 算子没有经过adump接口时会调用此函数，将mc2和ffts相关信息重新刷新到memInfo；
void RefreshMc2CtxInfo(bool hasMc2Ctx, uint32_t skipNum, OpMemInfo &memInfo)
{
    memInfo.inputParamsAddrInfos.emplace_back(AddrInfo{0U, sizeof(HcclCombinOpParam), MemInfoSrc::BYPASS});
    memInfo.hasMc2Ctx = hasMc2Ctx;
    memInfo.skipNum = skipNum;
}

AdumpInfo GetAdumpInfo(const OpMemInfo &memInfo, const vector<uint8_t> &tilingData)
{
    if (!ArgsManager::Instance().HasAdumpInfo()) {
        ArgsManager::Instance().SetThroughAdumpFlag(false);
        DEBUG_LOG("Adump Addr map is null");
        return {};
    }

    ArgsManager::Instance().SetThroughAdumpFlag(true);
    if (memInfo.tilingDataSize == 0) {
        INFO_LOG("Meta tiling data size is 0, can not parser tiling data, return invalid adump info");
        return {};
    }

    if (tilingData.size() < memInfo.tilingDataSize) {
        INFO_LOG("Real tiling buff data size is %lu, "
                 "which less than meta tiling size %lu, return invalid adump info",
                 tilingData.size(), memInfo.tilingDataSize);
        return {};
    }
    return GetAdumpInfoFromTilingData(tilingData, memInfo);
}
}

void UpdateInputSizeWithTilingData(const vector<uint8_t> &tilingData, OpMemInfo &memInfo)
{
    auto adumpInfo = ::GetAdumpInfo(memInfo, tilingData);
    if (adumpInfo.addr == nullptr) {
        auto hasMc2Ctx = memInfo.hasMc2Ctx;
        auto skipNum = memInfo.skipNum;
        // 动态算子从 meta section 里解析的 shape 信息是无效的，需要 adump 信息来更新
        // shape。如果没有 adump 信息，需要把从 meta section 里解析的信息清空防止影响
        // 后续流程
        memInfo.Clear();
        if (hasMc2Ctx) { RefreshMc2CtxInfo(hasMc2Ctx, skipNum, memInfo); }
        DEBUG_LOG("Adump Addr is nullptr");
        return;
    }
    auto *dynamicInputsPtr =  static_cast<uint64_t *>(adumpInfo.addr);
    DEBUG_LOG("Get adump context space, space size is %u, addr info size is %zu",
              adumpInfo.argsSpace, memInfo.inputParamsAddrInfos.size());
    for (uint32_t i = 0; i < adumpInfo.argsSpace && i < memInfo.inputParamsAddrInfos.size(); i++) {
        if (memInfo.hasMc2Ctx && i == MC2_CONTEXT_PARAMS_INDEX) {
            memInfo.inputParamsAddrInfos[i].length = sizeof(HcclCombinOpParam);
            continue;
        }
        auto length = *(dynamicInputsPtr + i);
        DEBUG_LOG("input index:%u, adump addr length:%lu", i, length);
        memInfo.inputParamsAddrInfos[i].length = length;
    }
}

ArgsContextSP ArgsManager::CreateContext(aclrtFuncHandle funcHandle, aclrtArgsHandle argsHandle)
{
    if (funcHandle == nullptr || argsHandle == nullptr) {
        return nullptr;
    }
    auto ctx = make_shared<ArgsHandleContext>(funcHandle, argsHandle);
    contexts_[argsHandle] = ctx;
    return ctx;
}

ArgsContextSP ArgsManager::CreateContext(void *args, uint32_t argsSize, bool isDeviceArgs)
{
    if (args == nullptr || argsSize == 0) {
        return nullptr;
    }
    SetArgsSize(argsSize);
    auto ctx = make_shared<ArgsRawContext>(args, argsSize, isDeviceArgs);
    contexts_[args] = ctx;
    return ctx;
}

ArgsContextSP ArgsManager::CreateContext(void *args, uint32_t argsSize, const std::vector<aclrtPlaceHolderInfo> &placeHolderArray)
{
    if (args == nullptr || argsSize == 0) {
        return nullptr;
    }
    SetArgsSize(argsSize);
    auto ctx = make_shared<ArgsRawContext>(args, argsSize, placeHolderArray);
    contexts_[args] = ctx;
    return ctx;
}

ArgsContextSP ArgsManager::GetContext(aclrtArgsHandle argsHandle) const
{
    auto it = contexts_.find(argsHandle);
    if (it == contexts_.end()) {
        return nullptr;
    }
    return it->second;
}

bool ArgsManager::GetAdumpInfo(uint64_t index, AdumpInfo &info) const
{
    lock_guard<mutex> lock(adumpInfoMtx_);
    auto it = atomicIndexToAddr_.find(index);
    if (it == atomicIndexToAddr_.end()) {
        return false;
    }
    info = it->second;
    return true;
}

void ArgsManager::AddAdumpInfo(uint64_t index, const AdumpInfo &info)
{
    lock_guard<mutex> lock(adumpInfoMtx_);
    atomicIndexToAddr_[index] = info;
    latestAtomicIndex_ = index;
}

bool ArgsManager::HasAdumpInfo() const
{
    lock_guard<mutex> lock(adumpInfoMtx_);
    return !atomicIndexToAddr_.empty();
}

// 为了节约内存拷贝，这个函数不做空的判断了，外部调用者自己先判断是否非空
const AdumpInfo &ArgsManager::GetLatestAdumpInfo() const
{
    lock_guard<mutex> lock(adumpInfoMtx_);
    return atomicIndexToAddr_.at(latestAtomicIndex_);
}

void ArgsManager::ParseSecondPtrAddrs(const AclrtLaunchArgsInfo &launchArgs, OpMemInfo &opMemInfo, uint32_t index) const
{
    lock_guard<mutex> lock(adumpInfoMtx_);
    // 查找当前输入的index是否为二级指针，不是则返回
    auto infosIt = opMemInfo.secondPtrAddrInfos.find(index);
    if (infosIt == opMemInfo.secondPtrAddrInfos.cend()) {
        DEBUG_LOG("No second point address infomation for index=%u", index);
        return;
    }
    if (launchArgs.placeHolderArray == nullptr) {
        WARN_LOG("placeHolderArray is null");
        return;
    }
    auto *buff = static_cast<uint64_t *>(launchArgs.hostArgs);
    for (size_t i = 0; i < launchArgs.placeHolderNum; ++i) {
        auto curHostInfoPtr = launchArgs.placeHolderArray[i];
        uint64_t hostIndex = curHostInfoPtr.addrOffset / sizeof(uint64_t);
        if (hostIndex != uint64_t(index)) {continue;}
        auto &secondPtrInfo = infosIt->second;
        uint64_t *hostInputsPtr = buff + curHostInfoPtr.dataOffset / sizeof(uint64_t);
        uint64_t ptrOffset = *hostInputsPtr / sizeof(uint64_t);
        DEBUG_LOG("hostInput index:%lu addrOffset:%u dataOffset:%u ptrOffset:%lu", hostIndex,
            curHostInfoPtr.addrOffset, curHostInfoPtr.dataOffset, ptrOffset);
        uint64_t *dimCntPtr = hostInputsPtr + 1;
        size_t dimIdx = 0;
        size_t ptrIdx = 0;
        while (ptrOffset > 0 && dimIdx < ptrOffset - 1) {
            auto dimCntVal = dimCntPtr[dimIdx];
            std::ostringstream oss;
            uint32_t dim = static_cast<uint32_t>(dimCntVal & 0xFFFFFFFFULL);
            uint32_t cnt = static_cast<uint32_t>((dimCntVal >> 32) & 0xFFFFFFFFULL);
            oss << "dim:" << dim << " cnt:" << cnt;
            // 解析dim cnt之后更新dimIdx
            dimIdx++;
            uint64_t totalDim = 1;
            oss << " shape:[";
            for (size_t j = 0; j < dim; ++j) {
                auto dimVal = dimCntPtr[dimIdx + j];
                totalDim *= dimVal;
                oss << dimVal << ",";
            }
            oss << "] dtypeBytes:" << secondPtrInfo.dtypeBytes << std::hex << " addr:0x" <<
                hostInputsPtr[ptrOffset + ptrIdx] << std::dec;
            DEBUG_LOG("hostDfxInfo %.2048s", oss.str().c_str());
            secondPtrInfo.addrInfoVec.push_back(AddrInfo{hostInputsPtr[ptrOffset + ptrIdx],
                totalDim * secondPtrInfo.dtypeBytes, MemInfoSrc::EXTRA});
            // 解析dim * int64 之后更新dimIdx
            dimIdx += dim;
            ptrIdx++;
        }
    }
}

std::vector<AddrInfo> ArgsManager::GetMc2AddrInfosFromArgAddr(uint64_t addr) const {
    std::string socVersion = DeviceContext::Local().GetSocVersion();
    auto chipType = GetProductSeriesType(socVersion);
    // 910C
    if (static_cast<uint32_t>(chipType) & static_cast<uint32_t>(ChipProductType::ASCEND910_93_SERIES)) {
        return GetMc2AddrInfosFromArgAddr910C(addr);
    } else if (static_cast<uint32_t>(chipType) & static_cast<uint32_t>(ChipProductType::ASCEND910B_SERIES)) {
        return GetMc2AddrInfosFromArgAddr910B(addr);
    } else {
        // unsupport
        return {};
    }
}

std::vector<AddrInfo> ArgsManager::GetMc2AddrInfosFromArgAddr910B(uint64_t addr) const {
    void *hostData;
    if (aclrtMallocHostImplOrigin(&hostData, sizeof(HcclCombinOpParam)) != ACL_ERROR_NONE) {
        return {};
    }
    Defer defer0(nullptr, [&hostData](std::nullptr_t&) {
        if (aclrtFreeHostImplOrigin(hostData) != ACL_ERROR_NONE) {
            ERROR_LOG("rtFreeHost failed");
        }
    });
    aclError error = aclrtMemcpyImplOrigin(hostData, sizeof(HcclCombinOpParam),
        reinterpret_cast<void *>(addr), sizeof(HcclCombinOpParam), ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("%s rtMemcpy error: %d", __FUNCTION__, error);
        return {};
    }
    auto opParamPtr = reinterpret_cast<HcclCombinOpParam *>(hostData);
    if (opParamPtr->multiFlag != 0U) {
        ERROR_LOG("multiFlag is :%u, not 0, currently unable to parse mc2 address",
            static_cast<uint32_t>(opParamPtr->multiFlag));
        return {};
    }
    std::vector<AddrInfo> mc2CtxAddrs;
    /// 共享内存信息来源如果算子经过adump接口，则内存来源为extra，否则应为BYPASS，保证上报成功
    bool isThroughAdump = ArgsManager::Instance().GetThroughAdumpFlag();
    MemInfoSrc infoSrc = isThroughAdump ? MemInfoSrc::EXTRA : MemInfoSrc::BYPASS;
    for (size_t i = 0; i < opParamPtr->rankNum; ++i) {
        // 如果未经过adump接口，则当前卡的共享内存有rtMalloc，需要过滤掉对应的当前卡的bypass上报，否则会illegal free
        if (!isThroughAdump && opParamPtr->rankId == i) {
            continue;
        }
        mc2CtxAddrs.push_back({opParamPtr->windowsIn[i], opParamPtr->winSize, infoSrc, MemInfoDesc::HCCL_MC2_CONTEXT});
        mc2CtxAddrs.push_back({opParamPtr->windowsOut[i], opParamPtr->winSize, infoSrc, MemInfoDesc::HCCL_MC2_CONTEXT});
    }
    return mc2CtxAddrs;
}

std::vector<AddrInfo> ArgsManager::GetMc2AddrInfosFromArgAddr910C(uint64_t addr) const {
    void *hostData;
    if (aclrtMallocHostImplOrigin(&hostData, sizeof(HcclOpResParam)) != ACL_ERROR_NONE) {
        return {};
    }
    Defer defer0(nullptr, [&hostData](std::nullptr_t &) {
        if (aclrtFreeHostImplOrigin(hostData) != ACL_ERROR_NONE) {
            ERROR_LOG("rtFreeHost failed");
        }
    });
    aclError error = aclrtMemcpyImplOrigin(hostData, sizeof(HcclOpResParam), reinterpret_cast<void *>(addr),
        sizeof(HcclOpResParam), ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("%s, rtMemcpy error: %d", __FUNCTION__, error);
        return {};
    }
    auto opParamPtr = reinterpret_cast<HcclOpResParam *>(hostData);
    std::vector<AddrInfo> mc2CtxAddrs;
    /// 共享内存信息来源如果算子经过adump接口，则内存来源为extra，否则应为BYPASS，保证上报成功
    bool isThroughAdump = ArgsManager::Instance().GetThroughAdumpFlag();
    MemInfoSrc infoSrc = isThroughAdump ? MemInfoSrc::EXTRA : MemInfoSrc::BYPASS;
    DEBUG_LOG("rankSize=%u, localUsrRankId=%u, winSize=%lu", opParamPtr->rankSize, opParamPtr->localUsrRankId,
        opParamPtr->winSize);
    void *remoteResData = nullptr;
    if (aclrtMallocHostImplOrigin(&remoteResData, sizeof(HcclRankRelationResV2)) != ACL_ERROR_NONE) {
        ERROR_LOG("Parse910CMc2CtxAddrs malloc host for HcclRankRelationResV2 failed");
        return {};
    }
    Defer defer1(nullptr, [&remoteResData](std::nullptr_t &) {
        if (aclrtFreeHostImplOrigin(remoteResData) != ACL_ERROR_NONE) {
            ERROR_LOG("rtFreeHost failed");
        }
    });
    for (size_t i = 0; i < opParamPtr->rankSize; ++i) {
        // 如果未经过adump接口，则当前卡的共享内存有rtMalloc，需要过滤掉对应的当前卡的bypass上报，否则会illegal free
        if (!isThroughAdump && i == opParamPtr->localUsrRankId) {
            continue;
        }
        uint64_t nextDevicePtr = opParamPtr->remoteRes[i].nextDevicePtr;
        if (nextDevicePtr == 0U) {
            continue;
        }
        aclError cpErr = aclrtMemcpyImplOrigin(remoteResData, sizeof(HcclRankRelationResV2),
            reinterpret_cast<void *>(nextDevicePtr), sizeof(HcclRankRelationResV2), ACL_MEMCPY_DEVICE_TO_HOST);
        if (cpErr != ACL_ERROR_NONE) {
            ERROR_LOG("%s rtMemcpy remoteRes[%zu] error: %d", __FUNCTION__, i, cpErr);
            continue;
        }
        auto rankRelPtr = reinterpret_cast<HcclRankRelationResV2 *>(remoteResData);
        DEBUG_LOG("remoteRes[%zu] windowsIn=%#lx, winSize=%lu", i, rankRelPtr->windowsIn, opParamPtr->winSize);
        mc2CtxAddrs.push_back({rankRelPtr->windowsIn, opParamPtr->winSize, infoSrc, MemInfoDesc::HCCL_MC2_CONTEXT});
    }
    return mc2CtxAddrs;
}
