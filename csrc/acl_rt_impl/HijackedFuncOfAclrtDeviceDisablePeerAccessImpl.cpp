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
#include "runtime/inject_helpers/InteractHelper.h"
#include "runtime/inject_helpers/ThreadContext.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "acl_rt_impl/AclRuntimeConfig.h"
#include "utils/Protocol.h"
#include "utils/InjectLogger.h"

HijackedFuncOfAclrtDeviceDisablePeerAccessImpl::HijackedFuncOfAclrtDeviceDisablePeerAccessImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtDeviceDisablePeerAccessImpl") {}

void HijackedFuncOfAclrtDeviceDisablePeerAccessImpl::Pre(int32_t peerDeviceId) {
    deviceId_ = ThreadContext::Instance().GetDeviceId();
    peerDeviceId_ = peerDeviceId;
}

aclError HijackedFuncOfAclrtDeviceDisablePeerAccessImpl::Post(aclError ret) {
    if (ret == ACL_SUCCESS && IsSanitizer()) {
        PeerAccessRecord record{deviceId_, peerDeviceId_, PeerAccessOp::DISABLE};
        MemoryManage::RevokeP2PMappings(deviceId_, peerDeviceId_);
        PeerAccessState::Instance().Apply(deviceId_, peerDeviceId_, PeerAccessOp::DISABLE);
        InteractHelper::Notify(deviceId_, PacketType::PEER_ACCESS_RECORD, record);
    }
    return ret;
}

// 调用原接口；符号缺失时按既有劫持接口约定输出错误并返回失败
aclError HijackedFuncOfAclrtDeviceDisablePeerAccessImpl::Call(int32_t peerDeviceId) {
    Pre(peerDeviceId);
    if (originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfAclrtDeviceDisablePeerAccessImpl originfunc is nullptr.");
        return EmptyFunc();
    }
    return Post(originfunc_(peerDeviceId));
}
