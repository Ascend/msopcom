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

#include "FusionLaunchArgs.h"

#include <algorithm>
#include <cstring>
#include "utils/Protocol.h"

bool FusionLaunchArgs::Init(const void *fusionInfo, const rtFusionArgsEx_t *argsInfo)
{
    // 判断工具能否适配，失败后由 hook 原样转发。
    if (!fusionInfo || !argsInfo || !argsInfo->args || argsInfo->argsSize == 0 ||
        argsInfo->argsSize > MAX_ALL_PARAM_SIZE || argsInfo->isNoNeedH2DCopy != 0 || argsInfo->aicpuNum != 0 ||
        (argsInfo->hostInputInfoNum != 0 && !argsInfo->hostInputInfoPtr)) {
        return false;
    }
    const auto &tasks = *static_cast<const rtFunsionTaskInfo_t *>(fusionInfo);
    if (!ValidateTasks(tasks)) { return false; }
    const auto &aic = tasks.subTask[aicoreIndex_].task.aicoreInfo;
    if (!ParseAttributes(*aic.config) || !ComputeLayout(*argsInfo)) { return false; }
    BuildCopy(tasks, *argsInfo);
    return true;
}

bool FusionLaunchArgs::ValidateTasks(const rtFunsionTaskInfo_t &tasks)
{
    // 当前工具仅适配一个 CCU 和一个 AIC 子任务，先校验子任务数量。
    if (tasks.subTaskNum != FUSION_SUB_TASK_MAX_NUM) {
        return false;
    }
    uint32_t aicCount = 0;
    uint32_t ccuCount = 0;
    for (uint32_t i = 0; i < tasks.subTaskNum; ++i) {
        if (tasks.subTask[i].type == RT_FUSION_AICORE) {
            aicoreIndex_ = i;
            ++aicCount;
        } else if (tasks.subTask[i].type == RT_FUSION_CCU) {
            ++ccuCount;
        }
    }
    if (aicCount != 1 || ccuCount != 1) {
        return false;
    }
    // 校验AIC标识：有句柄才能拿到注册kernel；只支持注册句柄入口，不支持函数指针入口；配置和属性有效性校验
    const auto &aic = tasks.subTask[aicoreIndex_].task.aicoreInfo;
    if (!aic.hdl || aic.stubFunc || !aic.config || !aic.config->attrs || aic.config->numAttrs == 0 ||
        aic.config->numAttrs > RT_LAUNCH_ATTRIBUTE_MAX) {
        return false;
    }
    return true;
}

bool FusionLaunchArgs::ParseAttributes(const rtLaunchConfig_t &config)
{
    // 按属性 id 读取 BLOCKDIM 和动态 UB 大小，供记录区初始化使用。
    blockDim_ = 0;
    // 未配置动态 UB 时使用零；每次解析都重置，避免复用对象时沿用上次配置。
    dynUbufSize_ = 0;
    for (uint32_t i = 0; i < config.numAttrs; ++i) {
        const auto &attr = config.attrs[i];
        if (attr.id == RT_LAUNCH_ATTRIBUTE_BLOCKDIM) {
            if (blockDim_ != 0 || attr.value.blockDim == 0) {
                return false;
            }
            blockDim_ = attr.value.blockDim;
        } else if (attr.id == RT_LAUNCH_ATTRIBUTE_DYN_UBUF_SIZE) {
            dynUbufSize_ = attr.value.dynUBufSize;
        }
    }
    if (blockDim_ == 0) {
        return false;
    }
    return true;
}

bool FusionLaunchArgs::ComputeLayout(const rtFusionArgsEx_t &args)
{
    // 参数布局约定为 head 在前、内联数据在后。
    // 取最早的内联数据偏移作为 head 终点；无内联描述时，整个原参数缓冲区均视作 head。
    headSize_ = args.argsSize;
    for (uint16_t i = 0; i < args.hostInputInfoNum; ++i) {
        const auto &input = args.hostInputInfoPtr[i];
        if (input.dataOffset >= args.argsSize) {
            return false;
        }
        headSize_ = std::min(headSize_, input.dataOffset);
    }
    // 每个待回填地址槽必须8字节对齐，且完整位于 head 内，不能跨入内联数据区。
    // 先检查 addrOffset 不大于 headSize，再相减，避免无符号减法下溢。
    for (uint16_t i = 0; i < args.hostInputInfoNum; ++i) {
        const auto &input = args.hostInputInfoPtr[i];
        if (input.addrOffset % sizeof(uint64_t) != 0 || input.addrOffset > headSize_ ||
            headSize_ - input.addrOffset < sizeof(uint64_t)) {
            return false;
        }
    }
    // 每条 host-input 描述通过 addrOffset 指定参数头中要写入指针的位置，通过 dataOffset 指定内联数据的位置。
    // addrOffset 不能重复，避免 runtime 多次改写同一个指针；dataOffset 可以重复，允许多个指针指向同一份数据。
    std::vector<uint32_t> destinations;
    for (uint16_t i = 0; i < args.hostInputInfoNum; ++i) {
        destinations.push_back(args.hostInputInfoPtr[i].addrOffset);
    }
    std::sort(destinations.begin(), destinations.end());
    if (std::adjacent_find(destinations.begin(), destinations.end()) != destinations.end()) {
        return false;
    }
    // 记录指针放在 head 之后的8字节对齐位置；此时只计算偏移，不插入记录参数。
    recordOffset_ = (headSize_ + 7U) / 8U * 8U;
    return true;
}

void FusionLaunchArgs::BuildCopy(const rtFunsionTaskInfo_t &tasks, const rtFusionArgsEx_t &args)
{
    const auto &aic = tasks.subTask[aicoreIndex_].task.aicoreInfo;
    // 深拷贝副本：工具后续插桩会改变参数，只改副本，原始调用者的数据不修改
    // 提交前如果失败需要使用原始任务和数据回退，副本在中间过程中可能已经被修改，无法回退
    tasks_ = tasks;
    args_ = args;
    kernelArgs_ = {};
    expanded_ = false;
    attrs_.assign(aic.config->attrs, aic.config->attrs + aic.config->numAttrs);
    hostInputs_.clear();
    if (args.hostInputInfoNum != 0) {
        hostInputs_.assign(args.hostInputInfoPtr, args.hostInputInfoPtr + args.hostInputInfoNum);
    }
    const auto *bytes = static_cast<const uint8_t *>(args.args);
    bytes_.clear();
    // 一次预留最终容量：原总长 + 对齐补齐 + 8Byte记录指针，避免重新分配导致该地址失效。
    bytes_.reserve(args.argsSize + recordOffset_ - headSize_ + sizeof(uint64_t));
    bytes_.assign(bytes, bytes + args.argsSize);
    // 外层结构复制把调用者的指针也复制进来，需要将指针重连到自己的副本。
    config_.numAttrs = attrs_.size();
    config_.attrs = attrs_.data();
    Aicore().config = &config_;
    Refresh();
}

void FusionLaunchArgs::Refresh()
{
    // 维护两种 ABI 的视图：fusion launch 和普通下发使用两套结构体，不能强转。
    // runtime 对融合参数副本的地址回填，也能由普通视图在后处理时读到
    args_.args = bytes_.data();
    args_.argsSize = bytes_.size();
    args_.hostInputInfoPtr = hostInputs_.empty() ? nullptr : hostInputs_.data();
    kernelArgs_.args = args_.args;
    kernelArgs_.argsSize = args_.argsSize;
    kernelArgs_.hostInputInfoPtr = args_.hostInputInfoPtr;
    kernelArgs_.hostInputInfoNum = args_.hostInputInfoNum;
    // 普通参数视图额外保存的 hasTiling、tiling 偏移由 SetTiling 和 AppendRecord 维护，此处保留。
}

bool FusionLaunchArgs::SetTiling(uint64_t paramsNo)
{
    // 序号从 1 开始；0 表示元数据未指定 tiling。非零序号仅支持可定位的内联 tiling。
    // 找不到内联描述时返回 false，由 hook 原样转发；这不代表原始 runtime 调用必然非法。
    kernelArgs_.hasTiling = 0;
    if (paramsNo == 0) {
        return true;
    }
    // 验证地址槽完整落在原参数 head 内
    if (paramsNo > headSize_ / sizeof(uint64_t)) {
        return false;
    }
    // 用地址槽偏移找到tiling地址和数据
    const uint32_t offset = (paramsNo - 1) * sizeof(uint64_t);
    for (const auto &input : hostInputs_) {
        if (input.addrOffset == offset) {
            kernelArgs_.hasTiling = 1;
            kernelArgs_.tilingAddrOffset = offset;
            kernelArgs_.tilingDataOffset = input.dataOffset;
            return true;
        }
    }
    return false;
}

bool FusionLaunchArgs::AppendRecord(uint8_t *record)
{
    // 同一次适配只允许追加一次有效记录地址，避免重复插入导致偏移累积。
    if (!record || expanded_ || bytes_.empty()) {
        return false;
    }
    // 插入8字节+对齐补齐；原内联数据从 headSize_ 起整体后移。
    const uint32_t added = recordOffset_ - headSize_ + sizeof(uint64_t);
    bytes_.insert(bytes_.begin() + headSize_, added, 0);
    const uint64_t address = reinterpret_cast<uint64_t>(record);
    std::memcpy(bytes_.data() + recordOffset_, &address, sizeof(address));
    // 原 head 槽位置不变，只调整后移的数据位置；runtime 仍向原 addrOffset 槽回填地址。
    for (auto &input : hostInputs_) {
        input.dataOffset += added;
    }
    // 普通视图中的内联 tiling 偏移同步后移，其地址槽偏移保持不变。
    if (kernelArgs_.hasTiling != 0) {
        kernelArgs_.tilingDataOffset += added;
    }
    expanded_ = true;
    Refresh();
    return true;
}
