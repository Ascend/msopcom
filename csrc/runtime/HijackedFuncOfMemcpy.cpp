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

// 该文件主要实现注入函数的功能，其配合被劫持函数的别名，实现新的劫持函数功能

#include "HijackedFunc.h"
#include "core/FuncSelector.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"
#include "RuntimeConfig.h"
#include "runtime/inject_helpers/LocalDevice.h"
HijackedFuncOfMemcpy::HijackedFuncOfMemcpy()
    : HijackedFuncType(RuntimeConfig::Instance().Instance().soName_, "rtMemcpy") {}

void HijackedFuncOfMemcpy::Pre(void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind)
{
    if (IsSanitizer()) {
        PacketHead head = { PacketType::MEMORY_RECORD };
        HostMemRecord record{};
        record.infoSrc = MemInfoSrc::RT;
        record.memSize = cnt;
        if (kind == RT_MEMCPY_HOST_TO_DEVICE || kind == RT_MEMCPY_DEVICE_TO_DEVICE) {
            record.type = MemOpType::STORE;
            record.dstAddr = reinterpret_cast<uint64_t>(dst);
            LocalDevice::Local().Notify(Serialize(head, record));
        }
        if (kind == RT_MEMCPY_DEVICE_TO_HOST || kind == RT_MEMCPY_DEVICE_TO_DEVICE) {
            record.type = MemOpType::LOAD;
            record.dstAddr = reinterpret_cast<uint64_t>(src);
            LocalDevice::Local().Notify(Serialize(head, record));
        }
    }
}
