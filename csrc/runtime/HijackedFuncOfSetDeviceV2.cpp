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

#include <iostream>
#include "HijackedFunc.h"
#include "RuntimeConfig.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ThreadContext.h"

HijackedFuncOfSetDeviceV2::HijackedFuncOfSetDeviceV2()
    : HijackedFuncType(RuntimeLibName(), "rtSetDeviceV2"), devId_{0} { }

void HijackedFuncOfSetDeviceV2::Pre(int32_t devId, rtDeviceMode deviceMode)
{
    ThreadContext::Instance().SetDeviceId(devId);
    DeviceContext::Local().SetDeviceId(devId);
    this->devId_ = devId;
}

rtError_t HijackedFuncOfSetDeviceV2::Call(int32_t devId, rtDeviceMode deviceMode)
{
    Pre(devId, deviceMode);
    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfSetDeviceV2 originfunc is nullptr.");
        return RT_ERROR_RESERVED;
    }
    if (IsOpProf() && ProfConfig::Instance().IsSimulator()) {
        devId = 0;
    }
    rtError_t ret = this->originfunc_(devId, deviceMode);
    return Post(ret);
}
