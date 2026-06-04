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


#include "HijackedFunc.h"
#include "runtime/inject_helpers/DevMemManager.h"
#include "RuntimeConfig.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/ProfConfig.h"

HijackedFuncOfDeviceReset::HijackedFuncOfDeviceReset()
    : HijackedFuncType(RuntimeLibName(), "rtDeviceReset"), devId_{0} { }

void HijackedFuncOfDeviceReset::Pre(int32_t devId)
{
    this->devId_ = devId;
    if (IsSanitizer()) {
        DevMemManager::Instance().Free();
        DEBUG_LOG("calling free function of DevMemManager in HijackedFuncOfDeviceReset.");
    }
}

rtError_t HijackedFuncOfDeviceReset::Call(int32_t devId)
{
    Pre(devId);
    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfDeviceReset originfunc is nullptr.");
        return RT_ERROR_RESERVED;
    }
    if (IsOpProf() && ProfConfig::Instance().IsSimulator()) {
        devId = 0;
    }
    rtError_t ret = this->originfunc_(devId);
    return Post(ret);
}

rtError_t HijackedFuncOfDeviceReset::Post(rtError_t ret)
{
    if (IsSanitizer()) {
        if (ret != RT_ERROR_NONE) {
            return ret;
        }

        std::string socVersion = DeviceContext::Instance(devId_).GetSocVersion();
        PacketHead head = { PacketType::DEVICE_SUMMARY };
        DeviceSummary summary{};
        summary.device = static_cast<uint32_t>(GetDeviceTypeBySocVersion(socVersion));
        summary.deviceId = devId_;
        LocalDevice::GetInstance(devId_).Notify(Serialize(head, summary));
    }
    return ret;
}
