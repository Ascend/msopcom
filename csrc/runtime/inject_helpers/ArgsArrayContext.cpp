/* -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
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

#include "ArgsArrayContext.h"

#include <algorithm>

#include "utils/InjectLogger.h"

using namespace std;

namespace {
constexpr uint32_t ALIGN_SIZE = 8;
}

ArgsArrayContext::ArgsArrayContext(
    size_t paramCount, const std::vector<size_t> &paramOffsets, const std::vector<size_t> &paramSizes)
    : ArgsContext(), paramOffsets_(paramOffsets), paramSizes_(paramSizes), paramCount_(paramCount) {}

ArgsArrayContextSP ArgsArrayContext::Create(
    void **args, size_t paramCount, const std::vector<size_t> &paramOffsets, const std::vector<size_t> &paramSizes) {
    ArgsArrayContextSP ctx(new ArgsArrayContext(paramCount, paramOffsets, paramSizes));
    if (ctx == nullptr || !ctx->Init(args)) {
        return nullptr;
    }
    return ctx;
}

// 将参数指针数组按缓存的内核参数布局打包成连续缓冲，与 runtime 的 CopyKernelParamsToBuffer 语义保持一致
bool ArgsArrayContext::Init(void **args) {
    if (args == nullptr || paramCount_ == 0 || paramOffsets_.size() != paramCount_ ||
        paramSizes_.size() != paramCount_) {
        return false;
    }
    size_t totalSize = 0;
    for (size_t i = 0; i < paramCount_; ++i) {
        totalSize = std::max(totalSize, paramOffsets_[i] + paramSizes_[i]);
    }
    packedArgs_.assign(totalSize, 0);
    for (size_t i = 0; i < paramCount_; ++i) {
        if (args[i] == nullptr) {
            WARN_LOG("Args array[%lu] is null.", i);
            return false;
        }
        std::copy_n(static_cast<uint8_t *>(args[i]), paramSizes_[i], packedArgs_.data() + paramOffsets_[i]);
    }
    RebuildArgsArray();
    return true;
}

// 在打包缓冲末尾对齐处追加 param，原地修改 packedArgs_。应仅在 Clone 后的副本上调用。
bool ArgsArrayContext::ExpandArgs(void *param, size_t paramSize, uint32_t &paramOffset) {
    if (packedArgs_.empty() || param == nullptr || paramSize == 0) {
        return false;
    }
    const size_t alignSize = (packedArgs_.size() + ALIGN_SIZE - 1) / ALIGN_SIZE * ALIGN_SIZE;
    packedArgs_.resize(alignSize + paramSize, 0);
    std::copy_n(static_cast<const uint8_t *>(param), paramSize, packedArgs_.data() + alignSize);
    memInfoOffset_ = alignSize;
    RebuildArgsArray();
    paramOffset = static_cast<uint32_t>(alignSize);
    return true;
}

void ArgsArrayContext::RebuildArgsArray() {
    argsArray_.clear();
    if (packedArgs_.empty()) {
        return;
    }
    argsArray_.resize(paramCount_ + 1);
    for (size_t i = 0; i < paramCount_; ++i) {
        argsArray_[i] = packedArgs_.data() + paramOffsets_[i];
    }
    argsArray_[paramCount_] = (memInfoOffset_ > 0) ? (packedArgs_.data() + memInfoOffset_) : nullptr;
}

uint32_t ArgsArrayContext::GetLastParamOffset() {
    return static_cast<uint32_t>((packedArgs_.size() + ALIGN_SIZE - 1) / ALIGN_SIZE * ALIGN_SIZE);
}

bool ArgsArrayContext::Save(const std::string &outputPath, DumperContext &config, OpMemInfo &memInfo, bool isSink) {
    (void)isSink;
    for (size_t i = 0; i < memInfo.inputParamsAddrInfos.size(); i++) {
        auto &addrInfo = memInfo.inputParamsAddrInfos[i];
        size_t paramIdx = memInfo.skipNum + i;
        if (paramIdx >= paramCount_) {
            WARN_LOG("Save: param index %lu out of range, paramCount=%lu.", paramIdx, paramCount_);
            return false;
        }
        size_t offset = paramOffsets_[paramIdx];
        if (offset + sizeof(uint64_t) > packedArgs_.size()) {
            WARN_LOG("Save: param offset %lu exceeds packedArgs_ size %zu.", offset, packedArgs_.size());
            return false;
        }
        addrInfo.addr = *reinterpret_cast<const uint64_t *>(packedArgs_.data() + offset);
    }
    return DumpInputData(outputPath, memInfo.inputParamsAddrInfos, config);
}

bool ArgsArrayContext::GetTilingData(std::vector<uint8_t> &data) const { return false; }

ArgsContextSP ArgsArrayContext::Clone(void) const {
    ArgsArrayContextSP ret = std::make_shared<ArgsArrayContext>(*this);
    // 拷贝构造会把 packedArgs_ 的内容深拷贝，但 argsArray_ 中的指针仍指向原对象缓冲，
    // 需要重新指向克隆对象自己的 packedArgs_。
    ret->RebuildArgsArray();
    return ret;
}
