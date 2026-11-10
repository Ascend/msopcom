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
#include "core/HijackedFuncTemplate.h"
#include "acl.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "runtime/inject_helpers/DevMemManager.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "utils/InjectLogger.h"
#include "runtime/inject_helpers/InteractHelper.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"

HijackedFuncOfAclrtResetDeviceImpl::HijackedFuncOfAclrtResetDeviceImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtResetDeviceImpl"), devId_{0} {}

void HijackedFuncOfAclrtResetDeviceImpl::Pre(int32_t devId)
{
    this->devId_ = devId;
    if (IsSanitizer()) {
        DevMemManager::Instance().Free();
        DEBUG_LOG("calling free function of DevMemManager in HijackedFuncOfAclrtResetDeviceImpl.");
    }
}

aclError HijackedFuncOfAclrtResetDeviceImpl::Call(int32_t devId)
{
    Pre(devId);
    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfAclrtResetDeviceImpl originfunc is nullptr.");
        return EmptyFunc();
    }
    if (IsOpProf() && ProfConfig::Instance().IsSimulator()) {
        devId = 0;
    }
    aclError ret = this->originfunc_(devId);
    return Post(ret);
}

aclError HijackedFuncOfAclrtResetDeviceImpl::Post(aclError ret)
{
    if (IsSanitizer()) {
        if (ret != ACL_ERROR_NONE) {
            return ret;
        }

        if (!DeviceContext::IsPrimaryContextReleased(devId_)) {
            return ret;
        }

        DeviceContext::Instance(devId_).AdvanceContextGeneration();
        std::string socVersion = DeviceContext::Instance(devId_).GetSocVersion();
        PacketHead head = { PacketType::DEVICE_SUMMARY };
        DeviceSummary summary{};
        summary.device = static_cast<uint32_t>(GetDeviceTypeBySocVersion(socVersion));
        summary.deviceId = devId_;
        LocalDevice::GetInstance(devId_).Notify(Serialize(head, summary));
        PeerAccessRecord record{devId_, -1, PeerAccessOp::RESET};
        MemoryManage::ResetDevice(devId_);
        PeerAccessState::Instance().Apply(devId_, -1, PeerAccessOp::RESET);
        InteractHelper::Notify(devId_, PacketType::PEER_ACCESS_RECORD, record);
    }
    return ret;
}
