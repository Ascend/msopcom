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

#pragma once

#include <vector>

#include "runtime.h"
#include "utils/InjectLogger.h"

uint32_t GetAlignSize(uint32_t s);

bool IsCurrentChip310P();

uint32_t GetArgsSize(rtArgsEx_t *argsInfo);

bool ExpandArgs(rtArgsEx_t *argsInfo, std::vector<uint8_t> &argsWithMemInfo, uint8_t *memInfo,
                std::vector<rtHostInputInfo_t> &hostInput, uint32_t &memInfoOffset);

bool ExpandArgs(void *&args, uint32_t& argsSize, std::vector<uint8_t> &argsWithMemInfo, uint8_t *memInfo);

inline void LogRtArgsExt(const rtArgsEx_t *argsInfo)
{
    if (argsInfo == nullptr) { return; }
    DEBUG_LOG("argsInfo argsSize:%u, tilingAddrOffset:%u, tilingDataOffset:%u, hostInputInfoNum:%u, "
        "hasTiling:%u, isNoNeedH2DCopy:%u", argsInfo->argsSize, argsInfo->tilingAddrOffset, argsInfo->tilingDataOffset,
        argsInfo->hostInputInfoNum, static_cast<uint32_t>(argsInfo->hasTiling),
        static_cast<uint32_t>(argsInfo->isNoNeedH2DCopy));
}

uint32_t GetKernelParamNum(rtArgsEx_t *argsInfo);

uint32_t GetKernelParamNum(uint32_t argsSize);
