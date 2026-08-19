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

#include "ArgsRawContext.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "utils/Numeric.h"
#include "utils/InjectLogger.h"
using namespace std;

bool ArgsRawContext::GetTilingData(std::vector<uint8_t> &data) const
{
    // we do not know tiling data size, it store in meta section
    if (placeHolderArray_.empty() || data.empty()) {
        DEBUG_LOG("placeHolderArray_ size=%lu, tiling_data_size=%lu", placeHolderArray_.size(), data.size());
        return false;
    }
    // 最后一个 placeholder 对应的 dataOffset 处保存的是 tilingData
    aclrtPlaceHolderInfo tilingInfo{};
    for (auto const &placeholder : placeHolderArray_) {
        if (placeholder.addrOffset > tilingInfo.addrOffset) {
            tilingInfo.addrOffset = placeholder.addrOffset;
            tilingInfo.dataOffset = placeholder.dataOffset;
        }
    }
    const uint8_t *buff = static_cast<const uint8_t *>(args_);
    const uint8_t *tilingData = buff + tilingInfo.dataOffset;
    data.assign(tilingData, tilingData + data.size());
    return true;
}

bool ArgsRawContext::ExpandArgs(void *param, size_t paramSize, uint32_t &paramOffset)
{
    argsWithMemInfo_.clear();
    constexpr uint32_t ALIGN_SIZE = 8;
    if (argsSize_ > MAX_ALL_PARAM_SIZE) {
        ERROR_LOG("argsSize is over max size: %zu", MAX_ALL_PARAM_SIZE);
        return false;
    }
    uint32_t alignArgsSize = CeilByAlignSize<ALIGN_SIZE>(argsSize_);
    argsWithMemInfo_.resize(alignArgsSize, 0);
    // device 指针需要先搬运到host侧,再进行处理
    auto copyMode = aclrtMemcpyKind::ACL_MEMCPY_HOST_TO_HOST;
    if (isDeviceArgs_) {
        copyMode = aclrtMemcpyKind::ACL_MEMCPY_DEVICE_TO_HOST;
    }
    aclrtMemcpyImplOrigin(argsWithMemInfo_.data(), argsSize_, args_, argsSize_, copyMode);
    uint8_t *paramPtr = reinterpret_cast<uint8_t*>(param);
    if (placeHolderArray_.empty()) {
        paramOffset = argsWithMemInfo_.size();
    } else {
        // insert param after last placeholder addr ptr
        uint32_t lastAddrOffset {};
        for (auto const &placeholder : placeHolderArray_) {
            lastAddrOffset = std::max(lastAddrOffset, placeholder.addrOffset);
        }
        paramOffset = lastAddrOffset + sizeof(uintptr_t);
        if (DeviceContext::Local().NeedOverflowStatus()) { paramOffset += sizeof(uintptr_t); }
        for (size_t i = 0; i < placeHolderArray_.size(); i++) {
            placeHolderArray_[i].dataOffset += paramSize;
        }
    }

    argsWithMemInfo_.insert(argsWithMemInfo_.cbegin() + paramOffset, paramPtr, paramPtr + paramSize);
    argsSize_ = argsWithMemInfo_.size();
    if (!isDeviceArgs_) {
        args_ = argsWithMemInfo_.data();
        return true;
    }
    FreeArgs();
    // 如果原始是device侧指针，需要将此处的host指针移动到device侧
    aclError ret = aclrtMallocImplOrigin(&args_, argsSize_, aclrtMemMallocPolicy::ACL_MEM_MALLOC_HUGE_FIRST);
    if (ret != ACL_ERROR_NONE) {
        return false;
    }
    needFree_ = true;
    ret = aclrtMemcpyImplOrigin(args_, argsSize_, argsWithMemInfo_.data(), argsSize_,
                                aclrtMemcpyKind::ACL_MEMCPY_HOST_TO_DEVICE);
    if (ret != ACL_ERROR_NONE) {
        return false;
    }
    return true;
}

uint32_t ArgsRawContext::GetLastParamOffset()
{
    constexpr uint32_t ALIGN_SIZE = 8;
     if (argsSize_ > MAX_ALL_PARAM_SIZE) {
        ERROR_LOG("argsSize is over max size: %zu", MAX_ALL_PARAM_SIZE);
        return 0;
    }
    if (placeHolderArray_.empty()) {
        return CeilByAlignSize<ALIGN_SIZE>(argsSize_);
    }
    uint32_t lastAddrOffset {};
    for (auto const &placeHolder : placeHolderArray_) {
        lastAddrOffset = std::max(lastAddrOffset, placeHolder.addrOffset);
    }
    uint32_t paramOffset = lastAddrOffset + sizeof(uintptr_t);
    if (DeviceContext::Local().NeedOverflowStatus()) { paramOffset += sizeof(uintptr_t); }
    return paramOffset;
}

bool ArgsRawContext::Save(const std::string &outputPath, DumperContext &config, OpMemInfo &memInfo, bool isSink)
{
    (void)isSink;
    uint64_t *args;
    if (isDeviceArgs_) {
        // 如果原始是device侧指针，需要将此处的host指针移动到device侧
        void *hostArgs_;
        aclError ret = aclrtMallocHostImplOrigin(&hostArgs_, argsSize_);
        if (ret != ACL_ERROR_NONE) {
            return false;
        }
        ret = aclrtMemcpyImplOrigin(hostArgs_, argsSize_, args_, argsSize_, aclrtMemcpyKind::ACL_MEMCPY_DEVICE_TO_HOST);
        if (ret != ACL_ERROR_NONE) {
            aclrtFreeHostImplOrigin(hostArgs_);
            return false;
        }
        args = static_cast<uint64_t *>(hostArgs_);
    } else {
        args = static_cast<uint64_t *>(args_);
    }
    for (size_t i = 0; i < memInfo.inputParamsAddrInfos.size(); i++) {
        auto &addrInfo = memInfo.inputParamsAddrInfos[i];
        addrInfo.addr = *(args + memInfo.skipNum + i);
    }
    if (isDeviceArgs_) {
        aclrtFreeHostImplOrigin(args);
    }
    std::vector<uint8_t> tilingData;
    tilingData.resize(memInfo.tilingDataSize);
    return DumpInputData(outputPath, memInfo.inputParamsAddrInfos, config) &&
           // 如果有 tiling data，tiling data dump 需要成功
           (!GetTilingData(tilingData) || DumpTilingData(outputPath, tilingData, config));
}

ArgsContextSP ArgsRawContext::Clone(void) const
{
    std::shared_ptr<ArgsRawContext> ret = std::make_shared<ArgsRawContext>(*this);
    // 拷贝构造会把 argsCopy_/argsWithMemInfo_ 的内容拷贝一份，但 args_ 仍指向原对象内部的缓冲，
    // 需要把 args_ 修正到克隆对象自己的缓冲上，否则原对象析构后克隆对象会持有悬垂指针。
    if (args_ == argsCopy_.data()) {
        ret->args_ = ret->argsCopy_.data();
    } else if (args_ == argsWithMemInfo_.data()) {
        ret->args_ = ret->argsWithMemInfo_.data();
    }
    return ret;
}

void ArgsRawContext::FreeArgs()
{
    if (needFree_ && args_ != nullptr) {
        aclrtFreeImplOrigin(args_);
        args_ = nullptr;
        needFree_ = false;
    }
}

ArgsRawContext::~ArgsRawContext()
{
    FreeArgs();
}
