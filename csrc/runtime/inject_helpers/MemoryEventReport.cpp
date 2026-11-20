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

void ReportMemcpy(MemoryCopyEndpoint dst, MemoryCopyEndpoint src, uint64_t width, MemInfoSrc infoSrc, size_t height) {
    if (!dst.onDevice && !src.onDevice) {
        return;
    }
    PacketHead head = {PacketType::MEMORY_RECORD};
    HostMemRecord record{};
    record.infoSrc = infoSrc;
    record.memSize = width;
    auto reportRow = [&](const MemoryCopyEndpoint &endpoint, size_t row, MemOpType type) {
        if (!endpoint.onDevice) {
            return;
        }
        auto base = reinterpret_cast<uint64_t>(endpoint.addr);
        auto offset = static_cast<uint64_t>(row * endpoint.pitch);
        if (base <= UINT64_MAX - offset) {
            record.type = type;
            record.dstAddr = base + offset;
            LocalDevice::Local().Notify(Serialize(head, record));
        }
    };
    for (size_t row = 0; row < height; ++row) {
        reportRow(dst, row, MemOpType::STORE);
        reportRow(src, row, MemOpType::LOAD);
    }
}

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
