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

HijackedFuncOfGetDevice::HijackedFuncOfGetDevice()
    : HijackedFuncType(RuntimeLibName(), "rtGetDevice"), devId_{0} { }

void HijackedFuncOfGetDevice::Pre(int32_t *devId)
{
}

rtError_t HijackedFuncOfGetDevice::Call(int32_t *devId)
{
    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfGetDevice originfunc is nullptr.");
        return RT_ERROR_RESERVED;
    }
    rtError_t ret = this->originfunc_(devId);

    if (IsOpProf() && ProfConfig::Instance().IsSimulator()) {
        // If this line is missing, a context empty error will be reported.
        if (devId != nullptr) {
            *devId = DeviceContext::Local().GetDeviceId();
        } else {
            ERROR_LOG("HijackedFuncOfGetDevice devId is null, cannot assign device id.");
            return RT_ERROR_RESERVED;
        }
    }

    return Post(ret);
}
