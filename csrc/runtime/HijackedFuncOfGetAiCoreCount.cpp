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

#include <cinttypes>

#include "HijackedFunc.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "RuntimeConfig.h"
#include "inject_helpers/ProfConfig.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "runtime/inject_helpers/SimulatorCoreInfo.h"
// 被aclrt废弃，用aclrtGetDeviceInfo
HijackedFuncOfGetAiCoreCount::HijackedFuncOfGetAiCoreCount()
    : HijackedFuncType(RuntimeLibName(), "rtGetAiCoreCount") {}

rtError_t HijackedFuncOfGetAiCoreCount::Call(uint32_t *aiCoreCnt)
{
    if (IsOpProf() && ProfConfig::Instance().IsSimulator()) {
        std::string simSocVersion = ProfConfig::Instance().GetSocVersion();
        if (!simSocVersion.empty() && aiCoreCnt != nullptr) {
            auto iter310p = CORE_NUM_OF_310P.find(simSocVersion);
            if (iter310p != CORE_NUM_OF_310P.end()) {
                *aiCoreCnt = iter310p->second;
                DEBUG_LOG("Replace AiCore count with [%" PRIu32 "]", *aiCoreCnt);
                return RT_ERROR_NONE;
            }

            uint32_t coreNum = 0;
            if (SimulatorSoc::TryGetSimulatorAiCoreNum(simSocVersion, coreNum)) {
                *aiCoreCnt = coreNum;
                DEBUG_LOG("Replace AiCore count with [%" PRIu32 "]", *aiCoreCnt);
                return RT_ERROR_NONE;
            }
        }
    }
    if (originfunc_ != nullptr) {
        return originfunc_(aiCoreCnt);
    }
    return RT_ERROR_RESERVED;
}
