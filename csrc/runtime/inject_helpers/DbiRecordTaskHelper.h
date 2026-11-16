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
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * ------------------------------------------------------------------------- */

#ifndef MSOPPROF_RUNTIME_INJECT_HELPERS_DBI_RECORD_TASK_HELPER_H
#define MSOPPROF_RUNTIME_INJECT_HELPERS_DBI_RECORD_TASK_HELPER_H

#include <algorithm>
#include <cstdint>
#include "include/opprof/DbiDefs.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "runtime/inject_helpers/DBITask.h"

namespace DbiRecordTaskHelper {

struct DbiRecordTask {
    ProfDBIType mode;
    const char *aclFailedLog;
};

static constexpr DbiRecordTask DBI_RECORD_TASKS[] = {
    {
        ProfDBIType::MEMORY_CHART,
        "Run dbi func failed"
    },
    {
        ProfDBIType::OPERAND_RECORD,
        "Run operand record func failed"
    },
    {
        ProfDBIType::WARP_TIMELINE,
        "Run warp timeline func failed"
    }
};

inline bool IsNeedGen(ProfDataCollect *profObj, ProfDBIType mode)
{
    switch (mode) {
        case ProfDBIType::MEMORY_CHART:
            return profObj->IsMemoryChartNeedGen();
        case ProfDBIType::OPERAND_RECORD:
            return profObj->IsOperandRecordNeedGen();
        case ProfDBIType::WARP_TIMELINE:
            return profObj->IsWarpTimelineNeedGen();
        default:
            return false;
    }
}

inline uint64_t GetDbiRecordMemSize(ProfDBIType mode, uint64_t numBlocks, uint64_t memoryChartCoreNum = 0)
{
    switch (mode) {
        case ProfDBIType::MEMORY_CHART: {
            uint64_t coreNum = memoryChartCoreNum == 0 ? GetCoreNumForDbi(numBlocks) : memoryChartCoreNum;
            const uint64_t blockDataSize = GetMemoryChartBlockDataSize(DeviceContext::Local().IsAscend950());
            return (blockDataSize + BLOCK_GAP) * coreNum;
        }
        case ProfDBIType::OPERAND_RECORD: {
            uint64_t sizePerAllType = static_cast<uint32_t>(OperandType::END) * sizeof(OperandRecord) +
                SIMT_THREAD_GAP;
            return sizeof(OperandHeader) + (sizePerAllType * (MAX_THREAD_NUM + 1) + BLOCK_GAP) *
                GetCoreNumForDbi(numBlocks);
        }
        case ProfDBIType::WARP_TIMELINE: {
            uint64_t blockSize = sizeof(WarpHeader) + WARP_NUM_PER_BLOCK * sizeof(WarpRecord) + BLOCK_GAP;
            return blockSize * GetCoreNumForDbi(numBlocks);
        }
        default:
            return 0;
    }
}

// MemoryChart 布局初始化：按当前 launch 对应二进制的识别结果预写布局标识。
// 识别失败时按含 SIMT 保守处理；预写失败返回 false，调用方必须终止本次 MemoryChart 回放。
inline bool InitMemoryChartLayout(ProfDataCollect *profObj, ProfDBIType mode, const RegisterContextSP &regCtx,
                                  uint8_t *memInfo, uint64_t memSize)
{
    if (mode != ProfDBIType::MEMORY_CHART) {
        return true;
    }
    // ACL 注册 ID 与 rt 注册表是两套编号，不能拿 ACL 的 regId 去查 rt 注册表；
    // ACL 入口直接使用当前 launch 自己的注册上下文，拿不到时按含 SIMT 保守处理。
    if (regCtx == nullptr) {
        return profObj->InitMemoryChartLayout(memInfo, memSize, false, false);
    }
    return profObj->InitMemoryChartLayout(memInfo, memSize, regCtx->IsSimtScanSucceeded(),
        regCtx->HasSimtSymbol());
}

// rt 入口的 regId 与注册表同源，按 launch 查到对应二进制后再取识别结果
inline bool InitMemoryChartLayout(ProfDataCollect *profObj, ProfDBIType mode, uint64_t regId,
                                  uint8_t *memInfo, uint64_t memSize)
{
    if (mode != ProfDBIType::MEMORY_CHART) {
        return true;
    }
    bool simtScanKnown = false;
    bool hasSimt = false;
    // 未命中时保持 false/false，由 InitMemoryChartLayout 按含 SIMT 保守布局处理
    KernelContext::Instance().GetRegisterSimtInfo(regId, simtScanKnown, hasSimt);
    return profObj->InitMemoryChartLayout(memInfo, memSize, simtScanKnown, hasSimt);
}

inline void CollectData(ProfDataCollect *profObj, ProfDBIType mode, uint64_t memSize, uint8_t *memInfo)
{
    switch (mode) {
        case ProfDBIType::MEMORY_CHART:
            profObj->GenDBIData(memSize, memInfo);
            break;
        case ProfDBIType::OPERAND_RECORD:
            profObj->GenRecordData(memSize, memInfo, OPERAND_RECORD);
            break;
        case ProfDBIType::WARP_TIMELINE:
            profObj->GenRecordData(memSize, memInfo, WARP_TIMELINE);
            break;
        default:
            break;
    }
}

inline const char *GetRtFailedLogPrefix(ProfDBIType mode)
{
    switch (mode) {
        case ProfDBIType::MEMORY_CHART:
            return "Memory chart kernel launch failed";
        case ProfDBIType::OPERAND_RECORD:
            return "Operand record kernel launch failed";
        case ProfDBIType::WARP_TIMELINE:
            return "Warp timeline kernel launch failed";
        default:
            return "Dbi record kernel launch failed";
    }
}

inline void AppendExtraInfo(const ProfDBIType &mode, const std::string &outputPath, std::string &tuneLogPath, std::vector<std::string> &extraArgs)
{
    if (mode == ProfDBIType::INSTR_PROF_START) {
        extraArgs = std::vector<std::string>{START_STUB_COMPILER_ARGS};
    } else if (mode == ProfDBIType::INSTR_PROF_DFX) {
        std::string instrPipeArg = "--dfx-region-instr-pipe=" + ProfConfig::Instance().GetInstrTimelinePipe();
        std::string regionIdMapArg = "--dfx-region-id-map=" + JoinPath({outputPath, DFX_REGION_MAP});
        extraArgs = std::vector<std::string>{instrPipeArg, regionIdMapArg};
        tuneLogPath = JoinPath({outputPath, DFX_TUNE_LOG});
    }
}

// ArgsArray 场景（参数以数组方式传入）下，动态插桩的 bisheng 命令需要额外追加
// --append-hbmout-paraminfo，使 bisheng-tune 能正确识别 HBM 输出参数信息。
// 注意：AppendExtraInfo 在部分 mode 下是对 extraArgs 的覆盖式赋值，因此本参数
// 必须在 AppendExtraInfo 之后统一追加，避免被覆盖丢失。
// 带去重：局部临时 vector 与全局复用的 DBITaskConfig::extraCompilerArgs_ 均可使用。
inline void AppendHbmOutParamInfoArgs(std::vector<std::string> &extraArgs) {
    if (std::find(extraArgs.begin(), extraArgs.end(), APPEND_HBMOUT_PARAMINFO_ARGS) == extraArgs.end()) {
        extraArgs.emplace_back(APPEND_HBMOUT_PARAMINFO_ARGS);
    }
}

// ArgsArray 场景下，把 --append-hbmout-paraminfo 追加到全局 DBITaskConfig 的
// 编译器附加参数中，供后续 RunDBITask / BBCountDumper 生成的 bisheng 命令使用。
inline void AppendHbmOutParamInfoToConfig() { AppendHbmOutParamInfoArgs(DBITaskConfig::Instance().extraCompilerArgs_); }
} // namespace DbiRecordTaskHelper

#endif // MSOPPROF_RUNTIME_INJECT_HELPERS_DBI_RECORD_TASK_HELPER_H
