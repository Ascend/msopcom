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
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"

namespace {
constexpr uint32_t MAX_DEVICE_CNT = 16U;
}

HijackedFuncOfGetDeviceCount::HijackedFuncOfGetDeviceCount()
    : HijackedFuncType(RuntimeLibName(), "rtGetDeviceCount") {}

rtError_t HijackedFuncOfGetDeviceCount::Call(int32_t *cnt)
{
    if (IsOpProf() && ProfConfig::Instance().IsSimulator()
        && (cnt != nullptr)) {
        *cnt = MAX_DEVICE_CNT;
        return Post(RT_ERROR_NONE);
    }

    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfGetDeviceCount originfunc is nullptr.");
        return RT_ERROR_RESERVED;
    }
    rtError_t ret = this->originfunc_(cnt);

    return Post(ret);
}
