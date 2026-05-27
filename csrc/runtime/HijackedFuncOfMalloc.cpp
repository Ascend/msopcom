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
#include "inject_helpers/KernelContext.h"
#include "inject_helpers/MemoryDataCollect.h"

HijackedFuncOfMalloc::HijackedFuncOfMalloc()
    : HijackedFuncType(RuntimeLibName(), "rtMalloc"), devPtr_{nullptr}, size_{} {}

void HijackedFuncOfMalloc::Pre(void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId)
{
    this->devPtr_ = devPtr;
    this->size_ = size;
    this->type_ = type;
}

rtError_t HijackedFuncOfMalloc::Call(void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId) {
    size_t actualSize = MemoryGuard::Instance().GetTotalSize(size);
    this->actualSize_ = actualSize;
    Pre(devPtr, size, type, moduleId);
    if (originfunc_) {
        rtError_t ret = originfunc_(devPtr, actualSize, type, moduleId);
        return Post(ret);
    }
    ERROR_LOG("HijackedFuncOfMalloc originfunc is nullptr.");

    return EmptyFunc();
}

rtError_t HijackedFuncOfMalloc::Post(rtError_t ret)
{
    if (IsSanitizer()) {
        // 只有实际内存分配成功内存地址才有效，才需要上报内存分配信息
        if (ret != RT_ERROR_NONE) {
            return ret;
        }

        MemoryGuard::Instance().MallocProc(devPtr_, size_);

        PacketHead head = { PacketType::MEMORY_RECORD };
        HostMemRecord record{};
        record.type = MemOpType::MALLOC;
        record.infoSrc = MemInfoSrc::RT;
        record.dstAddr = reinterpret_cast<uint64_t>(*devPtr_);
        record.memSize = size_;
        MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(record.dstAddr, record.infoSrc, record.memSize);
        LocalDevice::Local().Notify(Serialize(head, record));
    }
    if (IsOpProf() && !ProfConfig::Instance().IsSimulator() && ret == RT_ERROR_NONE) {
        if (!ProfConfig::Instance().IsAppReplay() && !KernelContext::Instance().GetLcclFlag() &&
            this->type_ != rtMemType_t::RT_MEMORY_P2P_HBM && this->type_ != rtMemType_t::RT_MEMORY_P2P_DDR) {
            MemoryContext::Instance().Append(*(this->devPtr_), this->size_);
        }
    }
    return ret;
}
