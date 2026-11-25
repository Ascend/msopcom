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
#include "ascend_hal/AscendHalOrigin.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "utils/Serialize.h"

void ReportMemcpy(MemoryCopyEndpoint dst, MemoryCopyEndpoint src, uint64_t width, MemInfoSrc infoSrc, size_t height) {
    bool reportDst = dst.onDevice;
    bool reportSrc = src.onDevice;
    if (!reportDst && !reportSrc) {
        return;
    }
    PacketHead head = {PacketType::MEMORY_RECORD};
    HostMemRecord record{};
    record.infoSrc = infoSrc;
    record.memSize = width;
    auto reportRow = [&](const MemoryCopyEndpoint &endpoint, size_t row, MemOpType type) {
        const auto base = reinterpret_cast<uint64_t>(endpoint.addr);
        uint64_t offset;
        uint64_t addr;
        if (__builtin_mul_overflow(row, endpoint.pitch, &offset) || __builtin_add_overflow(base, offset, &addr)) {
            WARN_LOG("Stop reporting memcpy %s rows at row %zu (pitch %zu): row address overflow.",
                type == MemOpType::STORE ? "destination" : "source", row, endpoint.pitch);
            return false;
        }
        record.type = type;
        record.dstAddr = addr;
        LocalDevice::Local().Notify(Serialize(head, record));
        return true;
    };
    for (size_t row = 0; row < height && (reportDst || reportSrc); ++row) {
        reportDst = reportDst && reportRow(dst, row, MemOpType::STORE);
        reportSrc = reportSrc && reportRow(src, row, MemOpType::LOAD);
    }
}

void ReportAclMemcpy(
    void *dst, const void *src, size_t width, aclrtMemcpyKind kind, size_t dpitch, size_t spitch, size_t height) {
    if (kind == ACL_MEMCPY_DEFAULT) {
        ReportAutoMemcpy(dst, src, width, MemInfoSrc::ACL, dpitch, spitch, height);
        return;
    }
    // Match the underlying ACL memcpy kind checks: INNER and INTER are not accepted as D2D.
    const bool d2d = kind == ACL_MEMCPY_DEVICE_TO_DEVICE;
    const bool dstOnDevice = d2d || kind == ACL_MEMCPY_HOST_TO_DEVICE || kind == ACL_MEMCPY_HOST_TO_BUF_TO_DEVICE;
    const bool srcOnDevice = d2d || kind == ACL_MEMCPY_DEVICE_TO_HOST;
    ReportMemcpy(
        {dst, ToMemSpace(dstOnDevice), dpitch}, {src, ToMemSpace(srcOnDevice), spitch}, width, MemInfoSrc::ACL, height);
}

void ReportAutoMemcpy(
    void *dst, const void *src, size_t width, MemInfoSrc infoSrc, size_t dpitch, size_t spitch, size_t height) {
    const auto querySpace = [](void *addr, const char *endpoint, MemSpace &space) {
        DVattribute attr{};
        const auto ret = drvMemGetAttributeOrigin(addr, &attr);
        if (ret != DRV_ERROR_NONE) {
            WARN_LOG("Skip DEFAULT memcpy reporting: %s memory attribute query for %p failed (ret=%d).", endpoint, addr,
                static_cast<int>(ret));
            return false;
        }
        space = ToMemSpace(attr.memType == DV_MEM_LOCK_DEV || attr.memType == DV_MEM_LOCK_DEV_DVPP);
        return true;
    };
    MemSpace dstSpace{};
    MemSpace srcSpace{};
    if (!querySpace(dst, "destination", dstSpace)) {
        return;
    }
    // The driver uses a non-const pointer for its read-only address attribute query.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    if (!querySpace(const_cast<void *>(src), "source", srcSpace)) {
        return;
    }
    ReportMemcpy({dst, dstSpace, dpitch}, {src, srcSpace, spitch}, width, infoSrc, height);
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
