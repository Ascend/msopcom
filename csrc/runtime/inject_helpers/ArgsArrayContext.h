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

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "ArgsContext.h"

class ArgsArrayContext;
using ArgsArrayContextSP = std::shared_ptr<ArgsArrayContext>;

/*
 * 管理 aclrtLaunchKernelWithArgsArray / aclrtLaunchSIMTKernelWithArgsArray 场景下
 * 的 kernel 入参：args 为参数指针数组（args[i] 指向第 i 个参数值）。
 * 本类负责将参数数组按内核参数布局打包成连续缓冲，并支持在末尾追加一个 GM 指针
 * （动态插桩的 memInfo），同时维护 launch 用的参数指针数组。
 *
 * 设计与 ArgsRawContext 一致：仅维护单份缓冲 packedArgs_，ExpandArgs 原地追加。
 * 需要插桩时应先 Clone 出独立副本再 ExpandArgs，避免污染原始上下文。
 */
class ArgsArrayContext : public ArgsContext {
public:
    static ArgsArrayContextSP Create(
        void **args, size_t paramCount, const std::vector<size_t> &paramOffsets, const std::vector<size_t> &paramSizes);

    // 在打包缓冲末尾对齐处追加 param，原地修改 packedArgs_ 并重建参数指针数组
    bool ExpandArgs(void *param, size_t paramSize, uint32_t &paramOffset) override;

    bool Save(const std::string &outputPath, DumperContext &config, OpMemInfo &memInfo, bool isSink) override;

    bool GetTilingData(std::vector<uint8_t> &data) const override;

    uint32_t GetLastParamOffset() override;

    ArgsContextSP Clone(void) const override;

    // 当前 launch 使用的参数指针数组
    void **GetArgsArray() { return argsArray_.empty() ? nullptr : argsArray_.data(); }

    bool HasArgsArray() const { return !argsArray_.empty(); }

    // 连续打包缓冲（供 sanitizer 上报 hostArgs 使用）
    void *GetPackedArgs() { return packedArgs_.empty() ? nullptr : packedArgs_.data(); }

    uint32_t GetPackedArgsSize() const { return static_cast<uint32_t>(packedArgs_.size()); }

    // 按算子入参序号(1-based)获取该参数在 packedArgs_ 中的字节偏移。
    // ArgsArray 路径无 placeholder，tiling 等参数的位置只能由 meta 解析出的
    // paramsNo 配合 paramOffsets_ 推得。paramsNo 越界时返回 false。
    bool GetParamOffsetByParamsNo(uint64_t paramsNo, size_t &offset) const;

private:
    ArgsArrayContext(size_t paramCount, const std::vector<size_t> &paramOffsets, const std::vector<size_t> &paramSizes);

    bool Init(void **args);

    void RebuildArgsArray();

    std::vector<uint8_t> packedArgs_; // 连续打包缓冲（可能已追加 memInfo）
    std::vector<void *> argsArray_; // launch 用的参数指针数组
    std::vector<size_t> paramOffsets_;
    std::vector<size_t> paramSizes_;
    size_t paramCount_{0};
    size_t memInfoOffset_{0}; // memInfo 在 packedArgs_ 中的偏移，0 表示未追加
};
