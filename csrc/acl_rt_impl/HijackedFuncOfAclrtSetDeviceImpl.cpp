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
#include "runtime/RuntimeOrigin.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/ThreadContext.h"
#include "utils/InjectLogger.h"

HijackedFuncOfAclrtSetDeviceImpl::HijackedFuncOfAclrtSetDeviceImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtSetDeviceImpl"), devId_{0} {}

void HijackedFuncOfAclrtSetDeviceImpl::Pre(int32_t devId)
{
    this->devId_ = devId;
    if (IsSanitizer()) {
        char const *socVersion = aclrtGetSocNameImplOrigin();
        if (std::string(socVersion).find("Ascend950") == std::string::npos) {
            return;
        }
        if (rtDeviceSetLimitOrigin(devId, RT_LIMIT_TYPE_SIMT_WARP_STACK_SIZE, 0x8000) != RT_ERROR_NONE) {
            ERROR_LOG("rtDeviceSetLimitOrigin call failed.");
        }
    }
}

aclError HijackedFuncOfAclrtSetDeviceImpl::Call(int32_t devId)
{
    Pre(devId);
    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfAclrtSetDeviceImpl originfunc is nullptr.");
        return EmptyFunc();
    }
    if (IsOpProf() && ProfConfig::Instance().IsSimulator()) {
        devId = 0;
    }
    aclError ret = this->originfunc_(devId);
    return Post(ret);
}

aclError HijackedFuncOfAclrtSetDeviceImpl::Post(aclError ret)
{
    if (ret != ACL_SUCCESS) {
        return ret;
    }
    ThreadContext::Instance().SetDeviceId(this->devId_);
    DeviceContext::Instance(devId_).SetDeviceId(this->devId_);
    if (IsSanitizer()) {
        char const *socVersion = aclrtGetSocNameImplOrigin();
        if (socVersion == nullptr) {
            ERROR_LOG("get soc version of device id %d failed", this->devId_);
            return ret;
        }

        uint64_t validLen = GetValidLength(socVersion, SOC_VERSION_MAX);
        std::string validSocVersion(socVersion, validLen);

        DEBUG_LOG("get soc version %s with device id %d", validSocVersion.c_str(), this->devId_);
        DeviceContext::Local().SetSocVersion(validSocVersion);

        PacketHead head = { PacketType::DEVICE_SUMMARY };
        DeviceSummary summary{};
        summary.device = static_cast<uint32_t>(GetDeviceTypeBySocVersion(socVersion));
        summary.deviceId = this->devId_;
        LocalDevice::GetInstance(this->devId_).Notify(Serialize(head, summary));
    }
    return ret;
}
