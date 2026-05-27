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
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/MemGuard.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"
#include "utils/InjectLogger.h"
#include "RuntimeConfig.h"
#include "runtime/RuntimeOrigin.h"
#include "inject_helpers/MemoryContext.h"
#include "inject_helpers/ProfConfig.h"
#include "inject_helpers/MemoryDataCollect.h"

HijackedFuncOfFree::HijackedFuncOfFree()
    : HijackedFuncType(RuntimeLibName(), "rtFree") {}

void HijackedFuncOfFree::Pre(void *devPtr)
{
    this->devPtr_ = devPtr;
    if (IsSanitizer()) {
        PacketHead head = { PacketType::MEMORY_RECORD };
        HostMemRecord record{};
        record.type = MemOpType::FREE;
        record.infoSrc = MemInfoSrc::RT;
        record.dstAddr = reinterpret_cast<uint64_t>(devPtr);
        MemoryManage::Instance().CacheMemory<MemoryOpType::FREE>(record.dstAddr, record.infoSrc);
        LocalDevice::Local().Notify(Serialize(head, record));

        MemoryGuard::Instance().FreeProc(devPtr_);
    }
    if (IsOpProf() && !ProfConfig::Instance().IsSimulator()) {
        MemoryContext::Instance().Discard(devPtr);
    }
}

rtError_t HijackedFuncOfFree::Call(void *devPtr) {
    void *actualPtr = MemoryGuard::Instance().GetRealPtr(devPtr);
    this->actualPtr_ = actualPtr;

    Pre(devPtr);
    if (originfunc_) {
        rtError_t ret = originfunc_(actualPtr);
        return Post(ret);
    }
    ERROR_LOG("HijackedFuncOfFree originfunc is nullptr.");

    return EmptyFunc();
}
