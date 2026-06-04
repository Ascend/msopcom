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
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/ProfConfig.h"

HijackedFuncOfCtxCreateV2::HijackedFuncOfCtxCreateV2()
    : HijackedFuncType(RuntimeLibName(), "rtCtxCreateV2"), devId_{0} { }

void HijackedFuncOfCtxCreateV2::Pre(void **createCtx, uint32_t flags, int32_t devId, rtDeviceMode deviceMode)
{
    DeviceContext::Local().SetDeviceId(devId);
    this->devId_ = devId;
}

rtError_t HijackedFuncOfCtxCreateV2::Call(void **createCtx, uint32_t flags, int32_t devId,
    rtDeviceMode deviceMode)
{
    Pre(createCtx, flags, devId, deviceMode);
    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfCtxCreateV2 originfunc is nullptr.");
        return RT_ERROR_RESERVED;
    }
    if (IsOpProf() && ProfConfig::Instance().IsSimulator()) {
        devId = 0;
    }
    rtError_t ret = this->originfunc_(createCtx, flags, devId, deviceMode);

    return Post(ret);
}
