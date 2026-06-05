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

#include <cstdint>
#include "include/opprof/DbiDefs.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ProfDataCollect.h"

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
            return BLOCK_MEM_SIZE * coreNum;
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

} // namespace DbiRecordTaskHelper

#endif // MSOPPROF_RUNTIME_INJECT_HELPERS_DBI_RECORD_TASK_HELPER_H
