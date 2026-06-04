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
#include "RuntimeConfig.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "runtime.h"

namespace {
}

HijackedFuncOfGetDeviceInfo::HijackedFuncOfGetDeviceInfo()
    : HijackedFuncType(RuntimeLibName(), "rtGetDeviceInfo") {}

rtError_t HijackedFuncOfGetDeviceInfo::Call(uint32_t deviceId, int32_t moduleType, int32_t infoType, int64_t *val)
{
    DEBUG_LOG("enter HijackedFuncOfGetDeviceInfo deviceId:%u moduleType:%d, infoType:%d",
        deviceId, moduleType, infoType);
    if (IsOpProf() && ProfConfig::Instance().IsSimulator()) {
        deviceId = 0;
        std::string simSocVersion = ProfConfig::Instance().GetSocVersion();
        if (CORE_NUM.count(simSocVersion) != 0 && val != nullptr) {
            if (moduleType == static_cast<int32_t>(tagRtDeviceModuleType::RT_MODULE_TYPE_AICORE)) {
                *val = CORE_NUM.at(simSocVersion);
                return RT_ERROR_NONE;
            }
            if (moduleType == static_cast<int32_t>(tagRtDeviceModuleType::RT_MODULE_TYPE_VECTOR_CORE)) {
                *val = CORE_NUM.at(simSocVersion) * 2;
                return RT_ERROR_NONE;
            }
        }
    }

    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfGetDeviceInfo originfunc is nullptr.");
        return RT_ERROR_RESERVED;
    }
    rtError_t ret = this->originfunc_(deviceId, moduleType, infoType, val);

    return Post(ret);
}
