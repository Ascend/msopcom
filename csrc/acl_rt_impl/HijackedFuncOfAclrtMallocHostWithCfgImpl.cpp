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
#include "utils/Numeric.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/MemoryContext.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"

HijackedFuncOfAclrtMallocHostWithCfgImpl::HijackedFuncOfAclrtMallocHostWithCfgImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtMallocHostWithCfgImpl"), hostPtr_{nullptr}, size_{} {}

void HijackedFuncOfAclrtMallocHostWithCfgImpl::Pre(void **hostPtr, size_t size, aclrtMallocConfig *cfg)
{
    this->hostPtr_ = hostPtr;
    this->size_ = size;
}

aclError HijackedFuncOfAclrtMallocHostWithCfgImpl::Post(aclError ret)
{
    if (IsSanitizer()) {
        // 只有实际内存分配成功内存地址才有效，才需要上报内存分配信息
        if (ret != ACL_SUCCESS) {
            return ret;
        }

        constexpr uint64_t blockAlignSize = 32;
        PacketHead head = { PacketType::MEMORY_RECORD };
        HostMemRecord record{};
        record.type = MemOpType::MALLOC;
        record.infoSrc = MemInfoSrc::ACL;
        record.dstAddr = reinterpret_cast<uint64_t>(*hostPtr_);
        // acl 接口内存分配上报时 size 与 rt 接口保持一致
        record.memSize = CeilByAlignSize<blockAlignSize>(size_) + blockAlignSize;
        LocalDevice::Local().Notify(Serialize(head, record));
    }
    return ret;
}
