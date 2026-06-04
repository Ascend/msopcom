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

#include "ascend_hal/HijackedFunc.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/RuntimeConfig.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"

HijackedFuncOfHalGetSocVersion::HijackedFuncOfHalGetSocVersion() : HijackedFuncType("ascend_hal", "halGetSocVersion") {}

drvError_t HijackedFuncOfHalGetSocVersion::Call(uint32_t devId, char *version, const uint32_t maxLen) {
    if (IsOpProf() && ProfConfig::Instance().IsSimulator()) {
        std::string simSocVersion = ProfConfig::Instance().GetSocVersion();
        if (version != nullptr && !simSocVersion.empty() && maxLen >= (simSocVersion.length() + 1)) {
            std::copy_n(simSocVersion.c_str(), simSocVersion.length() + 1, version);
            DEBUG_LOG("SocVersion replace success, version is %.2048s", version);
            return DRV_ERROR_NONE;
        }
    }
    if (originfunc_ != nullptr) {
        return originfunc_(devId, version, maxLen);
    }
    return DRV_ERROR_NOT_SUPPORT;
}
