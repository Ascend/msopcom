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

#include "acl_rt_impl/HijackedFunc.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "utils/InjectLogger.h"

HijackedFuncOfAclrtMemP2PMapImpl::HijackedFuncOfAclrtMemP2PMapImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtMemP2PMapImpl") {}

void HijackedFuncOfAclrtMemP2PMapImpl::Pre(void *devPtr, size_t size, int32_t dstDevId, uint64_t)
{
    addr_ = reinterpret_cast<uint64_t>(devPtr);
    size_ = size;
    deviceId_ = dstDevId;
}

aclError HijackedFuncOfAclrtMemP2PMapImpl::Post(aclError ret)
{
    DEBUG_LOG("P2PMap: ret=%d accessDevice=%d addr=0x%lx size=%lu", ret, deviceId_, addr_, size_);
    if (ret == ACL_SUCCESS && IsSanitizer()) {
        if (!MemoryManage::ApplyP2PMapping(deviceId_, addr_, size_)) {
            ERROR_LOG("Cannot associate successful P2PMap with one tracked allocation: device=%d addr=0x%lx size=%lu",
                deviceId_, addr_, size_);
        }
    }
    return ret;
}
