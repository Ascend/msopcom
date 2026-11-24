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

// 该文件主要实现注入函数的功能，其配合被劫持函数的别名，实现新的劫持函数功能

#include "HijackedFunc.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/FuncSelector.h"
#include "utils/Numeric.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"
#include "utils/InjectLogger.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/MemoryContext.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/MemGuard.h"

HijackedFuncOfAclrtMallocAlign32Impl::HijackedFuncOfAclrtMallocAlign32Impl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtMallocAlign32Impl"), devPtr_{nullptr}, size_{} {}

void HijackedFuncOfAclrtMallocAlign32Impl::Pre(void **devPtr, size_t size, aclrtMemMallocPolicy policy) {
    this->devPtr_ = devPtr;
    this->size_ = size;
    this->policy_ = policy;
}

aclError HijackedFuncOfAclrtMallocAlign32Impl::Call(void **devPtr, size_t size, aclrtMemMallocPolicy policy) {
    size_t actualSize = MemoryGuard::Instance().GetTotalSize(size);
    this->actualSize_ = actualSize;
    Pre(devPtr, size, policy);
    if (originfunc_) {
        aclError ret = originfunc_(devPtr, actualSize, policy);
        return Post(ret);
    }
    ERROR_LOG("HijackedFuncOfAclrtMallocAlign32 originfunc is nullptr.");

    return EmptyFunc();
}

aclError HijackedFuncOfAclrtMallocAlign32Impl::Post(aclError ret) {
    if (IsSanitizer()) {
        // 只有实际内存分配成功内存地址才有效，才需要上报内存分配信息
        if (ret != ACL_ERROR_NONE) {
            return ret;
        }

        MemoryGuard::Instance().MallocProc(devPtr_, size_);

        constexpr uint64_t blockAlignSize = 32;
        PacketHead head = {PacketType::MEMORY_RECORD};
        HostMemRecord record{};
        record.type = MemOpType::MALLOC;
        record.infoSrc = MemInfoSrc::ACL;
        record.dstAddr = reinterpret_cast<uint64_t>(*devPtr_);
        // aclrtMallocAlign32 仅对用户申请的 size 做 32 字节对齐，不会额外增加 32 字节
        record.memSize = CeilByAlignSize<blockAlignSize>(size_);
        MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(record.dstAddr, record.infoSrc, record.memSize);
        LocalDevice::Local().Notify(Serialize(head, record));
    }
    if (IsOpProf() && !ProfConfig::Instance().IsSimulator() && ret == ACL_SUCCESS) {
        if (!ProfConfig::Instance().IsAppReplay() && !KernelContext::Instance().GetLcclFlag() &&
            policy_ != ACL_MEM_MALLOC_HUGE_FIRST_P2P && policy_ != ACL_MEM_MALLOC_HUGE_ONLY_P2P &&
            policy_ != ACL_MEM_MALLOC_NORMAL_ONLY_P2P && policy_ != ACL_MEM_MALLOC_HUGE1G_ONLY_P2P) {
            MemoryContext::Instance().Append(*(this->devPtr_), this->size_);
        }
    }
    return ret;
}
