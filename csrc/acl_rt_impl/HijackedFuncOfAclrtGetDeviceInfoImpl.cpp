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
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/SimulatorCoreInfo.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "runtime.h"

namespace {
}

HijackedFuncOfAclrtGetDeviceInfoImpl::HijackedFuncOfAclrtGetDeviceInfoImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtGetDeviceInfoImpl") {}

aclError HijackedFuncOfAclrtGetDeviceInfoImpl::Call(uint32_t deviceId, aclrtDevAttr attr, int64_t *value)
{
    DEBUG_LOG("enter HijackedFuncOfGetDeviceInfo deviceId:%u aclrtDevAttr:%u", deviceId, static_cast<uint32_t>(attr));
    if (IsOpProf() && ProfConfig::Instance().IsSimulator() && value != nullptr) {
        deviceId = 0;
        std::string simSocVersion = ProfConfig::Instance().GetSocVersion();
        uint32_t coreNum = 0;
        const bool coreNumFound = SimulatorSoc::TryGetSimulatorAiCoreNum(simSocVersion, coreNum);

        if (coreNumFound && attr == static_cast<int32_t>(ACL_DEV_ATTR_AICORE_CORE_NUM)) {
            *value = coreNum;
            return ACL_SUCCESS;
        }
        if (coreNumFound && attr == static_cast<int32_t>(ACL_DEV_ATTR_VECTOR_CORE_NUM)) {
            *value = coreNum * 2;
            return ACL_SUCCESS;
        }
    }

    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfGetDeviceInfo originfunc is nullptr.");
        return ACL_ERROR_INTERNAL_ERROR;
    }
    aclError ret = this->originfunc_(deviceId, attr, value);

    return Post(ret);
}
