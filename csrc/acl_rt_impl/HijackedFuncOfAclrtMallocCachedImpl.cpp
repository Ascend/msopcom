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
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "utils/InjectLogger.h"
#include "utils/Numeric.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/MemoryContext.h"
#include "runtime/inject_helpers/MemGuard.h"

HijackedFuncOfAclrtMallocCachedImpl::HijackedFuncOfAclrtMallocCachedImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtMallocCachedImpl"), devPtr_{nullptr}, size_{} {}

void HijackedFuncOfAclrtMallocCachedImpl::Pre(void **devPtr, size_t size, aclrtMemMallocPolicy policy)
{
    this->devPtr_ = devPtr;
    this->size_ = size;
    this->policy_ = policy;
}

aclError HijackedFuncOfAclrtMallocCachedImpl::Call(void **devPtr, size_t size, aclrtMemMallocPolicy policy)
{
    size_t actualSize = MemoryGuard::Instance().GetTotalSize(size);
    this->actualSize_ = actualSize;
    Pre(devPtr, size, policy);
    if (originfunc_) {
        aclError ret = originfunc_(devPtr, actualSize, policy);
        return Post(ret);
    }
    ERROR_LOG("HijackedFuncOfAclrtMallocCached originfunc is nullptr.");

    return EmptyFunc();
}

aclError HijackedFuncOfAclrtMallocCachedImpl::Post(aclError ret)
{
    if (IsSanitizer()) {
        // 仅在分配成功时登记本次新分配；失败时不读取输出地址。
        if (ret != ACL_ERROR_NONE) {
            return ret;
        }

        MemoryGuard::Instance().MallocProc(devPtr_, size_);

        // MemoryGuard 使用请求长度；下方沿用 aclrtMallocImpl 的上报长度规则。
        // 向上对齐到 32 字节；除 Ascend950 和 HUGE1G_ONLY（含 P2P）外，再加 32 字节。
        constexpr uint64_t blockAlignSize = 32;
        uint64_t memSize = CeilByAlignSize<blockAlignSize>(size_);
        if (!DeviceContext::Local().IsAscend950() && policy_ != ACL_MEM_MALLOC_HUGE1G_ONLY &&
            policy_ != ACL_MEM_MALLOC_HUGE1G_ONLY_P2P) {
            memSize += blockAlignSize;
        }
        ReportMalloc(reinterpret_cast<uint64_t>(*devPtr_), memSize, MemInfoSrc::ACL);
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
