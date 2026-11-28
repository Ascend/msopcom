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

#include "MemoryDataCollect.h"
#include "LocalDevice.h"
#include "MemoryContext.h"
#include "ProfConfig.h"
#include "core/FuncSelector.h"
#include "utils/Serialize.h"

void RecordMemoryMapping(void *addr, uint64_t size, MemInfoSrc infoSrc) {
    if (IsSanitizer()) {
        ReportMalloc(reinterpret_cast<uint64_t>(addr), size, infoSrc);
    }
    if (IsOpProf() && !ProfConfig::Instance().IsSimulator() && !ProfConfig::Instance().IsAppReplay() &&
        !KernelContext::Instance().GetLcclFlag()) {
        MemoryContext::Instance().Append(addr, size);
    }
}

void RecordMemoryUnmapping(void *addr, MemInfoSrc infoSrc) {
    if (IsSanitizer()) {
        ReportFree(reinterpret_cast<uint64_t>(addr), infoSrc);
    }
    if (IsOpProf() && !ProfConfig::Instance().IsSimulator()) {
        MemoryContext::Instance().Discard(addr);
    }
}
