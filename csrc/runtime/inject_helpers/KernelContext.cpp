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

#include "KernelContext.h"
 
#include <elf.h>
#include <fstream>
#include <mutex>
#include <iostream>
#include <functional>
#include <iterator>
#include <sstream>
#include <tuple>
#include <utility>
#include <thread>
 
#include "runtime/inject_helpers/ArgsHandleContext.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "runtime/inject_helpers/LaunchContext.h"
#include "utils/InjectLogger.h"
#include "utils/ElfLoader.h"
#include "utils/FileSystem.h"
#include "utils/PipeCall.h"
#include "utils/TypeTraits.h"
#include "utils/Ustring.h"
#include "runtime/RuntimeOrigin.h"
#include "MemoryDataCollect.h"
#include "DBITask.h"
#include "MsTx.h"
#include "core/FuncSelector.h"
#include "RegisterContext.h"
#include "LaunchManager.h"
#include "ArgsManager.h"
#include "DeviceContext.h"
using namespace std;

namespace {
constexpr uint32_t U32_MASK = ~0U;
constexpr uint8_t U64_BYTE_SIZE = 8U;
constexpr char const *MIX_AIC_TAIL = "_mix_aic";
constexpr char const *MIX_AIV_TAIL = "_mix_aiv";
using Defer = std::shared_ptr<void>;

enum class PtrType : uint64_t {
    PRIMARY = 0U,
    SECONDARY,
    SECONDARY_SHAPE
};

bool GetTilingKeyFromName(std::string kernelName, uint64_t &tilingKey)
{
    if (EndsWith(kernelName, MIX_AIC_TAIL) ||
        EndsWith(kernelName, MIX_AIV_TAIL)) {
        kernelName = kernelName.substr(0UL, kernelName.length() - 8UL);
    }

    std::vector<std::string> items;
    SplitString(kernelName, '_', items);
    if (items.size() < 2UL) {
        return false;
    }
    std::stringstream ss(items[items.size() - 1UL]);
    ss >> tilingKey;
    return true;
}

bool GetKernelOffsetByTilingKey(std::vector<std::string> const &kernelNames,
                                std::vector<uint64_t> const &kernelOffsets,
                                uint64_t tilingKey,
                                uint64_t &kernelOffset)
{
    uint64_t tilingKeyFromName {};
    for (std::size_t idx = 0; idx < kernelNames.size(); ++idx) {
        if (!GetTilingKeyFromName(kernelNames[idx], tilingKeyFromName)) {
            continue;
        }
        if (tilingKeyFromName == tilingKey) {
            DEBUG_LOG("match kernel name with tiling key success. tilingKey: %lu, "
                      "kernelName: %s, kernelOffset: 0x%lx",
                      tilingKey, kernelNames[idx].c_str(), kernelOffsets[idx]);
            kernelOffset = kernelOffsets[idx];
            return true;
        }
    }
    return false;
}

void CompleteInputData(const rtArgsEx_t &argsInfo, OpMemInfo &memInfo)
{
    auto *buff = static_cast<uint64_t *>(argsInfo.args);
    uint8_t *tilingDataOffsetBuff = (argsInfo.tilingDataOffset == 0) ?
        nullptr : reinterpret_cast<uint8_t *>(buff + ((argsInfo.tilingDataOffset) / U64_BYTE_SIZE));
    uint8_t *tilingAddrOffsetBuff = (argsInfo.tilingAddrOffset == 0) ?
        nullptr : reinterpret_cast<uint8_t *>(*(buff + (argsInfo.tilingAddrOffset / U64_BYTE_SIZE)));
    uint8_t *tilingBuffData = (tilingDataOffsetBuff != nullptr) ? tilingDataOffsetBuff : tilingAddrOffsetBuff;
    vector<uint8_t> tilingData;
    if (tilingBuffData && memInfo.tilingDataSize > 0) {
        DEBUG_LOG("update input size with tiling data. tilingDataSize=%lu", memInfo.tilingDataSize);
        tilingData.assign(tilingBuffData, tilingBuffData + memInfo.tilingDataSize);
        UpdateInputSizeWithTilingData(tilingData, memInfo);
    }
}
}

std::string KernelContext::GetNameByTilingKey(const KernelHandle *handle, uint64_t tilingKey)
{
    // default is empty, keep same with msopt
    auto it = hdlToRegId_.find(handle);
    if (it == hdlToRegId_.end()) {
        WARN_LOG("can not find names for handle.");
        return "";
    }
    uint64_t regId = it->second;
    RegisterEvent event = registerEvents_[regId];

    const vector<string> &names = event.kernelNames;
    if (names.size() == 1U) {
        return names.back();
    }
    std::string nameSuffix = "_" + std::to_string(tilingKey);
    std::vector<std::string> suffixNames = {nameSuffix, nameSuffix + MIX_AIC_TAIL, nameSuffix + MIX_AIV_TAIL};
 
    for (const std::string &name : names) {
        for (const std::string &suffix: suffixNames) {
            if (EndsWith(name, suffix)) {
                return name;
            }
        }
    }
    return "";
}

bool KernelContext::GetDevBinary(KernelHandlePtr hdl, rtDevBinary_t &bin, bool getStubBin) const
{
    auto it = hdlToRegId_.find(hdl.value);
    if (it == hdlToRegId_.cend()) {
        ERROR_LOG("find register id by binary handle failed");
        return false;
    }
    RegisterEvent const &registerEvent = registerEvents_[it->second];
    if (getStubBin && registerEvent.stubBin == nullptr) {
        return false;
    }
    bin = getStubBin ? *registerEvent.stubBin : registerEvent.bin;
    return true;
}

bool KernelContext::GetDevBinary(StubFuncPtr stubFunc, rtDevBinary_t &bin, bool getStubBin) const
{
    auto it = stubInfo_.find(stubFunc.value);
    if (it == stubInfo_.cend()) {
        ERROR_LOG("find binary handle by stub func failed");
        return false;
    }
    return GetDevBinary(KernelHandlePtr{it->second.hdl}, bin, getStubBin);
}

bool KernelContext::GetLastLaunchEvent(LaunchEvent &event) const
{
    return this->GetDeviceContext().GetLastLaunchEvent(event);
}

KernelContext::DeviceContext::DeviceContext(KernelContext &kernelContext) : kernelContext_{kernelContext} {}

bool KernelContext::DeviceContext::GetLastLaunchEvent(LaunchEvent &event) const
{
    if (launchEvents_.empty()) {
        return false;
    }
    event = launchEvents_.back();
    return true;
}

bool KernelContext::GetStubFuncInfo(StubFuncPtr stubFunc, StubFuncInfo &stubFuncInfo) const
{
    auto it = stubInfo_.find(stubFunc.value);
    if (it == stubInfo_.cend()) {
        ERROR_LOG("find stub func info by stub func failed");
        return false;
    }

    stubFuncInfo = it->second;
    return true;
}

// infoAddr:  ...|input...|output|workspace|tiling ptr | overflow | tiling data
void KernelContext::SetArgsSize(const rtArgsSizeInfo_t *const sizeInfo)
{
    this->GetDeviceContext().SetArgsSize(sizeInfo);
}

void KernelContext::ClearArgsInfo()
{
    this->GetDeviceContext().ClearArgsInfo();
}

void KernelContext::DeviceContext::ParseMetaDataFromBinary(rtDevBinary_t const &binary, const rtArgsEx_t *argsInfo)
{
    if (memInfo_.isForSetException) {
        DEBUG_LOG("rtSetExceptionExtInfo is called, not parse binary file");
        SetFftsInfo(memInfo_, binary.magic);
        memInfo_.isForSetException = false;
        return;
    }
    memInfo_.Clear();
    std::string kernelName = KernelContext::Instance().GetLaunchName();
    std::vector<uint8_t> metaData;
    auto shSize = GetMetaSection(binary, kernelName, metaData);
    if (shSize == 0) {
        DEBUG_LOG("Get meta data failed");
        return;
    }
    auto parser = KernelMetaDataParser(metaData);
    if (parser.Parse()) {
        // overflow 地址信息不应该被覆盖
        OpMemInfo memInfo(memInfo_);
        memInfo_ = parser.GetOpMemInfo();
        memInfo_.hasOverflowAddr = memInfo.hasOverflowAddr;
        memInfo_.overflowAddr = memInfo.overflowAddr;
        SetFftsInfo(memInfo_, binary.magic);
        if (argsInfo) {
            CompleteInputData(*argsInfo, memInfo_);
        }
    }
}

void KernelContext::DeviceContext::SetArgsSize(const rtArgsSizeInfo_t * const sizeInfo)
{
    if (sizeInfo == nullptr || sizeInfo->infoAddr == nullptr) {
        return;
    }

    memInfo_.Clear();
    memInfo_.isForSetException = true;
    auto *buff = static_cast<uint64_t *>(sizeInfo->infoAddr);
 
    memInfo_.inputNum = buff[1] & U32_MASK;
    memInfo_.skipNum = (buff[1] >> 32U) & U32_MASK;

    // 二级指针场景解析
    uint64_t rightOperand = 56U;
    uint64_t inputOffset = 0U;
    for (uint64_t i = 0U; i < memInfo_.inputNum; ++i) {
        // 高8位存放指针类型信息，0表示1级指针，1表示无shape信息的二级指针，2表示带shape信息的二级指针
        uint64_t ptrType = (buff[2U + i + inputOffset] >> rightOperand) & 0xff;
        // 低56位表示一级指针的个数
        uint64_t primaryPtrNum = buff[2U + i + inputOffset] & 0xffffffffffffff;
        if (PtrType(ptrType) == PtrType::PRIMARY) {
            DEBUG_LOG("Ptr value is %lu", primaryPtrNum);
            memInfo_.inputParamsAddrInfos.emplace_back(AddrInfo{0U, primaryPtrNum, MemInfoSrc::EXTRA,
                MemInfoDesc::INPUT, i + 1});
        }
    }
    memInfo_.isFFTS = memInfo_.skipNum != 0;
}

bool KernelContext::DumpKernelObject(uint64_t regId, const string &outputPath)
{
    if (regId >= registerEvents_.size()) {
        return false;
    }
    const auto &regEvent = registerEvents_[regId];
    // fix flag in elf header
    vector<char> buffer(regEvent.binData.begin(), regEvent.binData.end());
    Elf64_Ehdr header{};
    if (!ElfLoader::LoadHeader(buffer, header)) {
        ERROR_LOG("Read elf header failed when load elf from buffer");
        return false;
    }
    // 编译算子时没有指定target， flag值会为这个
    if (header.e_flags == FLAG_DEFAULT_VALUE) {
        auto correctFlag = GetFlagByMagic(regEvent.bin.magic);
        if (correctFlag > 0) {
            header.e_flags = correctFlag;
            const char *base = static_cast<const char*>(static_cast<const void *>(&header));
            std::copy(base, base + sizeof(Elf64_Ehdr), buffer.data());
            DEBUG_LOG("default flag of op is forced set to %u", correctFlag);
        }
    }

    size_t written = WriteBinary(outputPath, reinterpret_cast<const char*>(buffer.data()), buffer.size());
    return written == buffer.size();
}

static bool UpdateSinkTaskArgsAddr(const uint64_t *args, OpMemInfo &memInfo)
{
    aclError ret = ACL_ERROR_NONE;
    for (size_t i = 0; i < memInfo.inputParamsAddrInfos.size(); i++) {
        auto devAddr = args + memInfo.skipNum + i;
        uint64_t argAddr;
        ret = aclrtMemcpyImplOrigin(static_cast<void *>(&argAddr), sizeof(uint64_t),
                                    static_cast<const void *>(devAddr), sizeof(uint64_t),
                                    ACL_MEMCPY_DEVICE_TO_HOST);
        if (ret != ACL_ERROR_NONE) {
            DEBUG_LOG("copy arg from device addr failed, ret=%u", ret);
            return false;
        }
        if (argAddr == 0 && memInfo.inputParamsAddrInfos[i].length > 0) {
            DEBUG_LOG("get invalid arg value from device addr");
            return false;
        }
        DEBUG_LOG("dev args[%lu] addr=%#lx, length=%lu", i, argAddr, memInfo.inputParamsAddrInfos[i].length);
        memInfo.inputParamsAddrInfos[i].addr = argAddr;
    }
    return true;
}

static bool UpdateNormalTaskArgsAddr(const uint64_t *args, OpMemInfo &memInfo)
{
    for (size_t i = 0; i < memInfo.inputParamsAddrInfos.size(); i++) {
        auto &addrInfo = memInfo.inputParamsAddrInfos[i];
        addrInfo.addr = *(args + memInfo.skipNum + i);
    }
    return true;
}

static bool DumpTilingData(const rtArgsEx_t &argsInfo, const string &outputDir, KernelContext::ContextConfig &config)
{
    auto *buff = static_cast<uint64_t *>(argsInfo.args);
    void *tilingDataOffsetBuff = (argsInfo.tilingDataOffset == 0) ?
                                 nullptr : static_cast<void *>(buff +
                                     ((argsInfo.tilingDataOffset) / sizeof(uint64_t)));
    void *tilingAddrOffsetBuff = (argsInfo.tilingAddrOffset == 0) ?
                                 nullptr : reinterpret_cast<void *>(*(buff +
                                     (argsInfo.tilingAddrOffset / sizeof(uint64_t))));
    void *tilingBuffData = (tilingDataOffsetBuff != nullptr) ? tilingDataOffsetBuff : tilingAddrOffsetBuff;
    if (tilingBuffData == nullptr) {
        DEBUG_LOG("no tiling data");
        return true;
    }
    if (argsInfo.argsSize <= argsInfo.tilingDataOffset) {
        return false;
    }
    uint32_t tilingDataSize = argsInfo.argsSize - argsInfo.tilingDataOffset;
    void *hostData;
    if (rtMallocHostOrigin(&hostData, tilingDataSize, 0) != RT_ERROR_NONE) {
        return false;
    }
    Defer defer0(nullptr, [&hostData](std::nullptr_t&) {
        if (rtFreeHostOrigin(hostData) != RT_ERROR_NONE) {
            ERROR_LOG("rtFreeHost failed.");
        }
    });
    if (rtMemcpyOrigin(hostData, tilingDataSize, tilingBuffData, tilingDataSize,
        RT_MEMCPY_DEVICE_TO_HOST) != RT_ERROR_NONE) {
        return false;
    }
 
    config.tilingDataPath = outputDir + "/input_tiling.bin";
    config.tilingDataSize = tilingDataSize;
    size_t written = WriteBinary(config.tilingDataPath, (const char *) hostData, tilingDataSize);
    if (written != tilingDataSize) {
        return false;
    }
    return true;
}

bool KernelContext::DeviceContext::DumpKernelArgs(const string &outputDir, uint64_t launchId,
    KernelContext::ContextConfig &config)
{
    if (launchId >= launchEvents_.size() || launchId >= memInfoHistory_.size()) {
        return false;
    }
    const auto &launchEvent = launchEvents_[launchId];
    auto argsInfo = launchEvent.argsInfoCopy;
    if (argsInfo.args == nullptr) {
        DEBUG_LOG("empty kernel args in argsInfo.");
        return false;
    }
    // infoAddr:  ...|input...|output|workspace|...
    const auto *args = static_cast<const uint64_t *>(argsInfo.args);
    auto &memInfo = memInfoHistory_[launchId];
    // 这里本质想判断args是不是Device上的args，从而走不一样的解析
    // 当前发现下沉图里aclgraph args是host的，所以不好判断
    // 当前只能针对走FlagV2的ge算子做这样的处理
    if (launchEvent.isSink && argsInfo.tilingDataOffset == 0) {
        // current, only flagv2 with graph mode
        return UpdateSinkTaskArgsAddr(args, memInfo) &&
               DumpInputData(outputDir, memInfo.inputParamsAddrInfos, config);
    } else {
        return UpdateNormalTaskArgsAddr(args, memInfo) &&
               DumpInputData(outputDir, memInfo.inputParamsAddrInfos, config) &&
               DumpTilingData(argsInfo, outputDir, config);
    }
}

bool KernelContext::Save(const string &outputDir, uint64_t launchId)
{
    return this->GetDeviceContext().Save(outputDir, launchId);
}

bool KernelContext::DeviceContext::Save(const string &outputDir, uint64_t launchId)
{
    if (launchId == UINT64_MAX) {
        launchId = GetLaunchId();
    }
    DEBUG_LOG("Start save launch event %lu", launchId);
    if (!SaveAicore(outputDir, launchId)) {
        return false;
    }
    // SaveAicore checked launchId and registerId
    ContextConfig config{};
    if (!DumpKernelArgs(outputDir, launchId, config)) {
        ERROR_LOG("Kernel context save failed, dump kernel args failed");
        return false;
    }
    if (launchId >= launchEvents_.size()) {
        DEBUG_LOG("Kernel context save fail because launch Id out of range");
        return false;
    }
    // set dump json on/off
    auto &launchEvent = launchEvents_[launchId];
    uint64_t regId = kernelContext_.GetRegisterId(launchEvent.hdl, true);
    if (regId >= this->kernelContext_.registerEvents_.size()) {
        DEBUG_LOG("Kernel context save fail because register id out of range");
        return false;
    }
    auto regEvent = this->kernelContext_.registerEvents_[regId];
    auto &devCtx = ::DeviceContext::Local();
    config.isFFTS = memInfoHistory_[launchId].isFFTS;
    config.magic = regEvent.bin.magic;
    config.blockDim = launchEvent.blockDim;
    config.kernelName = launchEvent.kernelName;
    config.tilingKey = launchEvent.tilingKey;
    config.binPath = JoinPath({outputDir, KERNEL_BIN_NAME});
    config.hasTilingKey = launchEvent.hasTilingKey;
    config.devId = devCtx.GetDeviceId();
    config.visibleDevId = devCtx.GetVisibleDeviceId();
    return DumpConfig(outputDir, config);
}

bool KernelContext::SaveAicore(const string &outputDir, uint64_t launchId)
{
    return this->GetDeviceContext().SaveAicore(outputDir, launchId);
}

bool KernelContext::DeviceContext::SaveAicore(const string &outputDir, uint64_t launchId)
{
    if (launchId == UINT64_MAX) {
        launchId = GetLaunchId();
    }
    if (launchId >= launchEvents_.size()) {
        ERROR_LOG("Kernel context save failed, invalid launch id");
        return false;
    }
    auto &launchEvent = launchEvents_[launchId];
    uint64_t regId = this->kernelContext_.GetRegisterId(launchEvent.hdl, true);
    if (regId >= this->kernelContext_.registerEvents_.size()) {
        ERROR_LOG("Kernel context save failed, regId is error");
        return false;
    }
    string kernelObjPath = JoinPath({outputDir, KERNEL_BIN_NAME});
    if (!this->kernelContext_.DumpKernelObject(regId, kernelObjPath)) {
        ERROR_LOG("Kernel context save failed, dump kernel obj failed");
        return false;
    }
    return true;
}

bool KernelContext::GetPcStartAddr(StubFuncPtr stubFunc, uint64_t &pcStartAddr)
{
    return this->GetDeviceContext().GetPcStartAddr(StubFuncArgs{stubFunc.value, nullptr}, pcStartAddr);
}

bool KernelContext::GetPcStartAddr(KernelHandlePtr hdl, uint64_t &pcStartAddr)
{
    auto it = as_const(hdlToRegId_).find(hdl.value);
    if (it == hdlToRegId_.cend()) {
        ERROR_LOG("find register id by handle failed");
        return false;
    }

    auto const &kernelNames = registerEvents_[it->second].kernelNames;
    if (kernelNames.empty()) {
        ERROR_LOG("get kernel name by register id failed");
        return false;
    }

    // get tiling key from first kernel name
    uint64_t tilingKey = 0UL;
    if (!GetTilingKeyFromName(kernelNames.front(), tilingKey)) {
        ERROR_LOG("get tiling key by kernel name failed");
        return false;
    }

    return this->GetDeviceContext().GetPcStartAddr(KernelHandleArgs{hdl.value, nullptr, tilingKey}, pcStartAddr);
}

bool KernelContext::DeviceContext::GetKernelAddr(StubFuncArgs const &args, uint64_t &kernelAddr) const
{
    void *pcStart = nullptr;
    uint32_t prefetchCnt{};

    StubFunc const *stubFunc = args.dbiHandle ? args.dbiHandle : args.originHandle;
    rtError_t ret = rtKernelGetAddrAndPrefCntOrigin(nullptr, 0UL, stubFunc, 0U, &pcStart, &prefetchCnt);
    if (ret != RT_ERROR_NONE) {
        ERROR_LOG("call rtKernelGetAddrAndPrefCntOrigin with stub func failed");
        // reset pcStartAddr to 0x00 if rtKernelGetAddrAndPrefCnt query failed
        kernelAddr = 0x00;
        return false;
    }
    kernelAddr = reinterpret_cast<uint64_t>(pcStart);
    DEBUG_LOG("get kernel addr by stub func success. kernelAddr: 0x%lx", kernelAddr);
    return true;
}

bool KernelContext::DeviceContext::GetKernelAddr(KernelHandleArgs const &args, uint64_t &kernelAddr) const
{
    void *pcStart = nullptr;
    uint32_t prefetchCnt{};

    KernelHandle const *hdl = args.dbiHandle ? args.dbiHandle : args.originHandle;
    rtError_t ret = rtKernelGetAddrAndPrefCntOrigin(const_cast<void *>(hdl), args.tilingKey,
                                                    nullptr, 1U, &pcStart, &prefetchCnt);
    if (ret != RT_ERROR_NONE) {
        ERROR_LOG("call rtKernelGetAddrAndPrefCntOrigin with tiling key failed");
        // reset pcStartAddr to 0x00 if rtKernelGetAddrAndPrefCnt query failed
        kernelAddr = 0x00;
        return false;
    }
    kernelAddr = reinterpret_cast<uint64_t>(pcStart);
    DEBUG_LOG("get kernel addr by tiling key success. tilingKey: %lu, kernelAddr: 0x%lx",
              args.tilingKey, kernelAddr);
    return true;
}

bool KernelContext::DeviceContext::GetKernelOffset(StubFuncArgs const &args, uint64_t &kernelOffset) const
{
    StubFuncInfo stubFuncInfo {};
    if (!kernelContext_.GetStubFuncInfo(StubFuncPtr{args.originHandle}, stubFuncInfo)) {
        return false;
    }
    uint64_t registerId = kernelContext_.GetRegisterId(stubFuncInfo.hdl, true);
    RegisterEvent registerEvent {};
    if (!kernelContext_.GetRegisterEvent(registerId, registerEvent)) {
        return false;
    }

    auto const &kernelNames = registerEvent.kernelNames;
    auto it = std::find(kernelNames.cbegin(), kernelNames.cend(), stubFuncInfo.kernelName);
    if (it == kernelNames.cend()) {
        return false;
    }
    kernelOffset = registerEvent.kernelOffsets[it - kernelNames.cbegin()];
    DEBUG_LOG("kernel offset from pc start addr: 0x%lx", kernelOffset);
    return true;
}

bool KernelContext::DeviceContext::GetKernelOffset(KernelHandleArgs const &args, uint64_t &kernelOffset) const
{
    uint64_t registerId = kernelContext_.GetRegisterId(args.originHandle, true);
    RegisterEvent registerEvent {};
    if (!kernelContext_.GetRegisterEvent(registerId, registerEvent)) {
        ERROR_LOG("get register event failed. registerId: %lu", registerId);
        return false;
    }

    auto const &kernelNames = registerEvent.kernelNames;
    auto const &kernelOffsets = registerEvent.kernelOffsets;
    if (!GetKernelOffsetByTilingKey(kernelNames, kernelOffsets, args.tilingKey, kernelOffset)) {
        ERROR_LOG("get kernel offset by tiling key failed. tilingKey: %lu", args.tilingKey);
        return false;
    }
    DEBUG_LOG("kernel offset from pc start addr: 0x%lx", kernelOffset);
    return true;
}

template <typename ArgsT>
bool KernelContext::DeviceContext::GetPcStartAddr(ArgsT const &args, uint64_t &pcStartAddr) const
{
    uint64_t kernelAddr {};
    if (!GetKernelAddr(args, kernelAddr)) {
        ERROR_LOG("get kernel addr failed");
        return false;
    }
    uint64_t kernelOffset {};
    if (!GetKernelOffset(args, kernelOffset)) {
        ERROR_LOG("get kernel offset from pc start failed");
        return false;
    }
    if (kernelAddr < kernelOffset) {
        ERROR_LOG("kernelAddr should never smaller than kernelOffset. 0x%lx vs. 0x%lx.",
                  kernelAddr, kernelOffset);
        return false;
    }
    pcStartAddr = kernelAddr - kernelOffset;
    DEBUG_LOG("get pc start addr 0x%lx", pcStartAddr);
    return true;
}

bool KernelContext::DumpKernelObject(const KernelHandle *hdl, const string &outputDir, const string &savedFileName)
{
    auto iter = hdlToRegId_.find(hdl);
    if (iter == hdlToRegId_.end()) {
        ERROR_LOG("Cannot find object reg id");
        return false;
    }
    if (!IsExist(outputDir) && !MkdirRecusively(outputDir)) {
        return false;
    }
    uint64_t regId = iter->second;
    return DumpKernelObject(regId, JoinPath({outputDir, savedFileName}));
}

std::string KernelContext::GetLaunchName(uint64_t launchId)
{
    return this->GetDeviceContext().GetLaunchName(launchId);
}

std::string KernelContext::DeviceContext::GetLaunchName(uint64_t launchId)
{
    if (launchId == UINT64_MAX) {
        launchId = GetLaunchId();
    }
    if (launchId >= launchEvents_.size()) {
        return "invalid_launch_id";
    }
    return launchEvents_[launchId].kernelName;
}

uint32_t KernelContext::GetBlockId()
{
    return this->GetDeviceContext().GetBlockId();
}

uint32_t KernelContext::DeviceContext::GetBlockId()
{
    uint64_t launchId = GetLaunchId();
    if (launchId == UINT64_MAX) {
        return UINT32_MAX;
    }
    return launchEvents_[launchId].blockDim;
}

bool KernelContext::GetLaunchEvent(uint64_t launchId, LaunchEvent &event) const
{
    return this->GetDeviceContext().GetLaunchEvent(launchId, event);
}

bool KernelContext::DeviceContext::GetLaunchEvent(uint64_t launchId, LaunchEvent &event) const
{
    if (launchId < launchEvents_.size()) {
        event = launchEvents_[launchId];
        return true;
    }
    return false;
}

uint64_t KernelContext::GetRegisterId(uint64_t launchId)
{
    return this->GetDeviceContext().GetRegisterId(launchId);
}

uint64_t KernelContext::DeviceContext::GetRegisterId(uint64_t launchId)
{
    std::lock_guard<std::mutex> guard(this->deviceMutex_);
    if (launchId >= launchEvents_.size()) {
        return UINT64_MAX;
    }
    auto event = launchEvents_[launchId];
    return this->kernelContext_.GetRegisterId(event.hdl, true);
}

uint64_t KernelContext::GetRegisterId(const void *key, bool withHandle)
{
    std::lock_guard<std::mutex> guard(this->mtx_);
    if (!withHandle) {
        auto it = stubInfo_.find(key);
        if (it == stubInfo_.end()) {
            return UINT64_MAX;
        }
        key = it->second.hdl;
    }
    auto it = hdlToRegId_.find(key);
    if (it == hdlToRegId_.end()) {
        return UINT64_MAX;
    }
    return it->second;
}

bool KernelContext::GetRegisterEvent(uint64_t regId, RegisterEvent &event)
{
    if (regId >= registerEvents_.size()) {
        return false;
    }
    event = registerEvents_[regId];
    return true;
}

uint64_t KernelContext::GetLaunchId() const
{
    return this->GetDeviceContext().GetLaunchId();
}

uint64_t KernelContext::DeviceContext::GetLaunchId() const
{
    if (launchEvents_.empty()) {
        return UINT64_MAX;
    }
    return launchEvents_.size() - 1;
}

static string GetFullKernelName(const char *name, const vector<string> &fullNames)
{
    if (!name) {
        return "";
    }
    uint64_t length = GetValidLength(name, MstxAPI::MAX_KERNEL_NAME_LENGTH);
    string subName(name, length);
    for (const auto &fullName: fullNames) {
        if (fullName == subName) {
            return fullName;
        }
        if (fullName.find(subName + MIX_AIC_TAIL) != fullName.npos) {
            return fullName;
        }
        if (fullName.find(subName + MIX_AIV_TAIL) != fullName.npos) {
            return fullName;
        }
    }
    return "";
}

void KernelContext::AddFuncRegisterEvent(const KernelHandle *binHdl, const StubFunc *stubFunc,
                                         const char *stubName, const void *kernelInfoExt, uint32_t funcMode)
{
    // to add default stub Name "anonymous"
    std::string stubNameStr("anonymous");
    if (stubName) {
        stubNameStr = stubName;
    }
    do {
        auto it = hdlToRegId_.find(binHdl);
        if (it == hdlToRegId_.end()) {
            break;
        }
        RegisterEvent event;
        if (!GetRegisterEvent(it->second, event)) {
            break;
        }
        string tmpName = GetFullKernelName(stubName, event.kernelNames);
        if (!tmpName.empty()) {
            stubNameStr = tmpName;
            break;
        }
        tmpName = GetFullKernelName(static_cast<const char *>(kernelInfoExt), event.kernelNames);
        if (!tmpName.empty()) {
            stubNameStr = tmpName;
            break;
        }
    } while (false);

    std::string kernelInfoExtStr{};
    if (kernelInfoExt) {
        kernelInfoExtStr = static_cast<const char *>(kernelInfoExt);
    }
    stubInfo_.insert({stubFunc, StubFuncInfo{binHdl, stubNameStr, kernelInfoExtStr, funcMode}});
}

void KernelContext::AddLaunchEvent(const StubFunc *stubFunc, uint32_t blockDim,
    const rtArgsEx_t *argsInfo, rtStream_t stm)
{
    auto it = stubInfo_.find(stubFunc);
    if (it == stubInfo_.end()) {
        return;
    }
    this->GetDeviceContext().AddLaunchEvent(stubFunc, it->second, blockDim, argsInfo, stm);
}

void KernelContext::DeviceContext::AddLaunchEvent(const StubFunc *stubFunc, StubFuncInfo const &info,
    uint32_t blockDim, const rtArgsEx_t *argsInfo, rtStream_t stm)
{
    uint64_t pcStartAddr {};
    if (!GetPcStartAddr(StubFuncArgs{stubFunc, nullptr}, pcStartAddr)) {
        WARN_LOG("get pc start addr by stub func failed. pc start addr will fallback to 0");
    }
    auto stmInfo = LaunchManager::GetOrCreateStreamInfo(stm);
    rtArgsEx_t argsInfoCopy{};
    if (argsInfo) {
        argsInfoCopy = *argsInfo;
    }
    launchEvents_.push_back({false, stubFunc, info.hdl, false, 0ULL,
                            pcStartAddr, argsInfo, blockDim, info.kernelName,
                            argsInfoCopy, stmInfo.binded});
}

void KernelContext::AddLaunchEvent(const KernelHandle *hdl, uint64_t tilingKey, uint32_t blockDim,
    const rtArgsEx_t *argsInfo, rtStream_t stm)
{
    std::string kernelName = GetNameByTilingKey(hdl, tilingKey);
    this->GetDeviceContext().AddLaunchEvent(hdl, tilingKey, blockDim, argsInfo, kernelName, stm);
}

void KernelContext::DeviceContext::AddLaunchEvent(const KernelHandle *hdl, uint64_t tilingKey, uint32_t blockDim,
    const rtArgsEx_t *argsInfo, std::string const &kernelName, rtStream_t stm)
{
    uint64_t pcStartAddr {};
    if (!GetPcStartAddr(KernelHandleArgs{hdl, nullptr, tilingKey}, pcStartAddr)) {
        WARN_LOG("get pc start addr by handle failed. pc start addr will fallback to 0");
    }
    auto stmInfo = LaunchManager::GetOrCreateStreamInfo(stm);
    rtArgsEx_t argsInfoCopy{};
    if (argsInfo) {
        argsInfoCopy = *argsInfo;
    }
    launchEvents_.push_back({true, nullptr, hdl, true, tilingKey,
                            pcStartAddr, argsInfo, blockDim, kernelName,
                            argsInfoCopy, stmInfo.binded});
}

bool KernelContext::GetMc2CtxFlag() const
{
    return this->GetDeviceContext().GetMc2CtxFlag();
}

void KernelContext::ParseSecondPtrAddrs(
    const rtArgsEx_t &argsInfo, OpMemInfo &opMemInfo, uint32_t index) const
{
     // 查找当前输入的index是否为二级指针，不是则返回
    auto infosIt = opMemInfo.secondPtrAddrInfos.find(index);
    if (infosIt == opMemInfo.secondPtrAddrInfos.cend()) {
        return;
    }
    if (argsInfo.hostInputInfoPtr == nullptr) {
        WARN_LOG("Host input ptr is null");
        return;
    }
    auto *buff = static_cast<uint64_t *>(argsInfo.args);
    for (auto i = 0; i < argsInfo.hostInputInfoNum; ++i) {
        auto curHostInfoPtr = argsInfo.hostInputInfoPtr[i];
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
                totalDim * secondPtrInfo.dtypeBytes, MemInfoSrc::EXTRA, MemInfoDesc::DOUBLE_PTR, ptrIdx + 1});
            // 解析dim * int64 之后更新dimIdx
            dimIdx += dim;
            ptrIdx++;
        }
    }
}

std::vector<AddrInfo> KernelContext::ParseMc2CtxAddrs(uint64_t addr) const
{
    void *hostData;
    if (rtMallocHostOrigin(&hostData, sizeof(HcclCombinOpParam), 0) != RT_ERROR_NONE) {
        return {};
    }
    Defer defer0(nullptr, [&hostData](std::nullptr_t&) {
        if (rtFreeHostOrigin(hostData) != RT_ERROR_NONE) {
            ERROR_LOG("rtFreeHost failed");
        }
    });
    rtError_t error = rtMemcpyOrigin(hostData, sizeof(HcclCombinOpParam),
        reinterpret_cast<void *>(addr), sizeof(HcclCombinOpParam), RT_MEMCPY_DEVICE_TO_HOST);
    if (error != RT_ERROR_NONE) {
        ERROR_LOG("ParseMc2CtxAddrs rtMemcpy error: %d", error);
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
    MemInfoSrc infoSrc =  isThroughAdump ? MemInfoSrc::EXTRA : MemInfoSrc::BYPASS;
    int32_t deviceId = 0;
    aclrtGetDeviceImplOrigin(&deviceId);
    for (size_t i = 0; i < opParamPtr->rankNum; ++i) {
        // 如果未经过adump接口，则当前卡的共享内存有rtMalloc，需要过滤掉对应的当前卡的bypass上报，否则会illegal free
        if (!isThroughAdump && static_cast<size_t>(deviceId) == i) { continue; }
        mc2CtxAddrs.push_back({opParamPtr->windowsIn[i], opParamPtr->winSize, infoSrc, MemInfoDesc::HCCL_MC2_CONTEXT});
        mc2CtxAddrs.push_back({opParamPtr->windowsOut[i], opParamPtr->winSize, infoSrc, MemInfoDesc::HCCL_MC2_CONTEXT});
    }
    return mc2CtxAddrs;
}

void KernelContext::AddHdlRegisterEvent(const KernelHandle *hdl, const rtDevBinary_t *bin,
    const rtDevBinary_t *stubBin)
{
    if (!hdl || !bin) {
        return;
    }

    if (bin->length >= MAX_MEM_BYTE_SIZE) {
        return;
    }

    auto binData = static_cast<const uint8_t *>(bin->data);
    RegisterEvent event;
    event.hdl = hdl;
    event.bin = *bin;
    event.stubBin = stubBin;
    if (binData) {
        event.binData = vector<uint8_t>(binData, binData + bin->length);
        event.bin.data = event.binData.data();
    }
    event.isKernelWithDBI = !HasStaticStub(*bin);
    event.hasDebugLine = HasDebugLine(*bin);
    GetSymInfoFromBinary(static_cast<const char*>(bin->data), bin->length, event.kernelNames, event.kernelOffsets);
    if (!event.kernelNames.empty()) {
        DEBUG_LOG("Register first kernelName is %s", event.kernelNames.front().c_str());
    }
    hasSimt_ = GetSimtSymbolFromBinary(static_cast<const char*>(bin->data), bin->length);
    hdlToRegId_[hdl] = registerEvents_.size();
    registerEvents_.push_back(std::move(event));
}

KernelContext::DeviceContext const &KernelContext::GetDeviceContext() const
{
    int32_t deviceId {};
    aclrtGetDeviceImplOrigin(&deviceId);

    std::lock_guard<std::mutex> guard(this->mtx_);
    auto it = this->deviceContextMap_.find(deviceId);
    if (it != this->deviceContextMap_.cend()) {
        return it->second;
    }
    auto p = this->deviceContextMap_.emplace(std::piecewise_construct,
                                             std::forward_as_tuple(deviceId),
                                             std::forward_as_tuple(*const_cast<KernelContext*>(this)));
    return p.first->second;
}

KernelContext::DeviceContext &KernelContext::GetDeviceContext()
{
    int32_t deviceId {};
    aclrtGetDeviceImplOrigin(&deviceId);

    std::lock_guard<std::mutex> guard(this->mtx_);
    auto it = this->deviceContextMap_.find(deviceId);
    if (it != this->deviceContextMap_.cend()) {
        return it->second;
    }
    auto p = this->deviceContextMap_.emplace(std::piecewise_construct,
                                             std::forward_as_tuple(deviceId),
                                             std::forward_as_tuple(*this));
    return p.first->second;
}

OpMemInfo &KernelContext::GetOpMemInfo()
{
    return this->GetDeviceContext().GetOpMemInfo();
}

OpMemInfo &KernelContext::DeviceContext::GetOpMemInfo()
{
    return this->memInfo_;
}

void KernelContext::SetMC2Flag()
{
    AicpuLaunchArgs& aicpuLaunchArgs = KernelContext::GetAicpuLaunchArgs();
    string aiCoreKernelName = "invalid_kernel_name";
    auto lastContext = LaunchManager::Local().GetLastContext();
    if (lastContext != nullptr && lastContext->GetFuncContext() != nullptr) {
        aiCoreKernelName = lastContext->GetFuncContext()->GetKernelName();
    }
    string aiCpuKernelName = aicpuLaunchArgs.opName;
    bool isMC2 = false;
    // 约定规则MC2成对下发,aicpu Kernel名称为{aicore kernel name}Mc2AicpuKernel
    // 这里考虑了AiCoreName带后缀的情况，类似KernelName_tilingKey_mix_aic
    if (EndsWith(aiCpuKernelName, MC2_AICPU_SUFFIX)) {
        string kernelName = aiCpuKernelName.substr(0, aiCpuKernelName.rfind(MC2_AICPU_SUFFIX));
        isMC2 = aicpuLaunchArgs.isValid && (aiCoreKernelName == kernelName ||
                StartsWith(aiCoreKernelName, kernelName + "_"));
    }
    this->GetDeviceContext().SetMC2Flag(isMC2);
}

bool KernelContext::GetMC2Flag()
{
    return this->GetDeviceContext().GetMC2Flag();
}

bool KernelContext::GetLcclFlag()
{
    return this->GetDeviceContext().GetLcclFlag();
}

void KernelContext::SetLcclFlag(const bool &isLccl)
{
    this->GetDeviceContext().SetLcclFlag(isLccl);
}

void KernelContext::ParseMetaDataFromBinary(const rtDevBinary_t &binary, const rtArgsEx_t *argsInfo)
{
    return this->GetDeviceContext().ParseMetaDataFromBinary(binary, argsInfo);
}

void KernelContext::ArchiveMemInfo()
{
    return this->GetDeviceContext().ArchiveMemInfo();
}

void KernelContext::DeviceContext::ListenCallbackTask(int32_t devId)
{
    auto ret = aclrtSetDeviceImplOrigin(devId);
    if (ret != ACL_ERROR_NONE) {
        ERROR_LOG("Set device to %d failed, ret=%u, stop listening",
            devId, ret);
        return;
    }
    DEBUG_LOG("set device id to %d for listen callback", devId);
    constexpr int32_t timeout = 1000; // ms
    while (!stopListen_) {
        ret = rtProcessReportOrigin(timeout);
        if (ret != RT_ERROR_NONE) {
            continue;
        }
        DEBUG_LOG("Finish one callback function");
    }
}

bool KernelContext::DeviceContext::SubscribeReport(rtStream_t stm)
{
    auto &streamInfo = LaunchManager::GetOrCreateStreamInfo(stm);
    if (streamInfo.subscribed) {
        return true;
    }
    stopListen_ = false;
    if (!listenThread_.joinable()) {
        listenThread_ = thread(&KernelContext::DeviceContext::ListenCallbackTask, this, ::DeviceContext::Local().GetDeviceId());
    }
    std::ostringstream oss;
    oss << listenThread_.get_id();
    uint64_t tid;
    if (!StringToNum(oss.str(), tid)) {
        ERROR_LOG("get thread id number failed: %s", oss.str().c_str());
        return false;
    }
    auto ret = rtSubscribeReportOrigin(tid, stm);
    if (ret != RT_ERROR_NONE) {
        DEBUG_LOG("subscribe report for stream failed");
        return false;
    }
    DEBUG_LOG("Subscribe report success.");
    streamInfo.subscribed = true;
    return true;
}

KernelContext::DeviceContext::~DeviceContext()
{
    stopListen_ = true;
    if (listenThread_.joinable()) {
        listenThread_.join();
    }
}

bool KernelContext::SetDbiBinary(uint64_t registerId, rtDevBinary_t const &devBinary)
{
    std::lock_guard<std::mutex> guard(this->mtx_);
    if (registerId >= registerEvents_.size()) {
        ERROR_LOG("registerId exceeds size of registerEvents. %lu vs. %zu.", registerId, registerEvents_.size());
        return false;
    }
    RegisterEvent &registerEvent = registerEvents_[registerId];
    // update stubBin binary
    registerEvent.stubBin = &devBinary;
    // update kernel names and kernel offsets by stub kernel
    registerEvent.kernelNames.clear();
    registerEvent.kernelOffsets.clear();
    GetSymInfoFromBinary(static_cast<const char*>(devBinary.data), devBinary.length,
                         registerEvent.kernelNames, registerEvent.kernelOffsets);
    return true;
}

template bool KernelContext::DeviceContext::UpdatePcStartAddrByDbi(uint64_t launchId, StubFuncArgs const &args);
template bool KernelContext::DeviceContext::UpdatePcStartAddrByDbi(uint64_t launchId, KernelHandleArgs const &args);

template <typename ArgsT>
bool KernelContext::DeviceContext::UpdatePcStartAddrByDbi(uint64_t launchId, ArgsT const &args)
{
    if (launchId >= launchEvents_.size()) {
        ERROR_LOG("launchId exceeds size of launchEvents. %lu vs. %zu.", launchId, launchEvents_.size());
        return false;
    }
    LaunchEvent &launchEvent = launchEvents_[launchId];
    uint64_t pcStartAddr {};
    if (!GetPcStartAddr(args, pcStartAddr)) {
        ERROR_LOG("update pc start addr by dbi failed. launchId: %lu", launchId);
        return false;
    }
    launchEvent.pcStartAddr = pcStartAddr;
    DEBUG_LOG("pc start addr for kernel with dbi: 0x%lx", pcStartAddr);
    return true;
}
