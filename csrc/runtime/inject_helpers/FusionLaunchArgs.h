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

#include <vector>
#include "runtime.h"

// 持有一次 CCU+AIC 下发的配置和参数副本，修改副本不影响调用者数据。
// 同一参数缓冲区分别提供融合下发和工具后处理视图，两种 ABI 结构不能强制转换。
class FusionLaunchArgs {
public:
    FusionLaunchArgs() = default;
    FusionLaunchArgs(const FusionLaunchArgs &) = delete;
    FusionLaunchArgs &operator=(const FusionLaunchArgs &) = delete;
    // 校验工具支持的任务、属性和布局，再建立副本；返回 false 时由 hook 使用原始参数转发。
    bool Init(const void *fusionInfo, const rtFusionArgsEx_t *argsInfo);
    // 根据从 1 开始的参数序号定位内联 tiling；0 表示未指定，非零但无法定位时返回 false。
    bool SetTiling(uint64_t paramsNo);
    // 在原参数头后追加一次有效记录指针，并移动内联数据、同步描述偏移；重复追加返回 false。
    bool AppendRecord(uint8_t *record);
    // 返回对象持有的下发副本及工具视图，指针的使用不得超过本对象生命周期。
    rtFunsionTaskInfo_t *Tasks() { return &tasks_; }
    rtFusionArgsEx_t *Args() { return &args_; }
    rtArgsEx_t *KernelArgs() { return &kernelArgs_; }
    rtAicoreFusionInfo_t &Aicore() { return tasks_.subTask[aicoreIndex_].task.aicoreInfo; }
    uint32_t BlockDim() const { return blockDim_; }
    uint32_t DynUbufSize() const { return dynUbufSize_; }
    uint32_t HeadSize() const { return headSize_; }
    uint32_t RecordOffset() const { return recordOffset_; }

private:
    // 仅接受一个 CCU 和一个通过注册句柄下发的 AIC，记录 AIC 位置并检查配置指针。
    bool ValidateTasks(const rtFunsionTaskInfo_t &tasks);
    // 提取必需的非零 BLOCKDIM 和可选动态 UB 大小；缺失或重复 BLOCKDIM 时返回 false。
    bool ParseAttributes(const rtLaunchConfig_t &config);
    // 确定原参数头及记录指针偏移，检查内联数据和回填地址槽边界，拒绝重复回填同一槽位。
    bool ComputeLayout(const rtFusionArgsEx_t &args);
    // 在校验通过后复制任务、属性、内联描述和参数字节，并将需要修改的指针重连至自有存储。
    void BuildCopy(const rtFunsionTaskInfo_t &tasks, const rtFusionArgsEx_t &args);
    // 让两种参数视图指向当前自有缓冲区；保留由 tiling 处理单独维护的状态和偏移。
    void Refresh();
    rtFunsionTaskInfo_t tasks_{};
    rtFusionArgsEx_t args_{};
    rtArgsEx_t kernelArgs_{};
    rtLaunchConfig_t config_{};
    std::vector<rtLaunchAttribute_t> attrs_;
    std::vector<rtHostInputInfo_t> hostInputs_;
    std::vector<uint8_t> bytes_;
    uint32_t aicoreIndex_ = 0;
    uint32_t blockDim_ = 0;
    uint32_t dynUbufSize_ = 0;
    uint32_t headSize_ = 0;
    uint32_t recordOffset_ = 0;
    bool expanded_ = false;
};
