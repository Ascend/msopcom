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
#include "runtime/inject_helpers/MemoryDataCollect.h"

HijackedFuncOfMemset::HijackedFuncOfMemset()
    : HijackedFuncType(RuntimeLibName(), "rtMemset") {}

void HijackedFuncOfMemset::Pre(void *devPtr, uint64_t destMax, uint32_t val, uint64_t cnt)
{
    if (IsSanitizer()) {
        if (!IsMemoryOnDevice(devPtr)) {
            return;
        }
        PacketHead head = { PacketType::MEMORY_RECORD };
        HostMemRecord record{};
        record.type = MemOpType::STORE;
        record.infoSrc = MemInfoSrc::RT;
        record.dstAddr = reinterpret_cast<uint64_t>(devPtr);
        record.memSize = cnt;
        LocalDevice::Local().Notify(Serialize(head, record));
    }
}
