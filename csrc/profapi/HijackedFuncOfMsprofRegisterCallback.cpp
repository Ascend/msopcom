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
#include "ProfInjectHelper.h"
#include "core/FuncSelector.h"
#include "include/thirdparty/prof.h"
#include "utils/InjectLogger.h"

HijackedFuncOfMsprofRegisterCallback::HijackedFuncOfMsprofRegisterCallback()
    : HijackedFuncType("profapi", "MsprofRegisterCallback") {}

void HijackedFuncOfMsprofRegisterCallback::Pre(uint32_t moduleId, ProfCommandHandle handle) {
    if (IsOpProf()) {
        ProfInjectHelper::Instance().handleMap_[moduleId] = handle;
        DEBUG_LOG("HijackedFuncOfMsprofRegisterCallback get module %u", moduleId);
        if (ProfInjectHelper::Instance().aicpuHandleCallMap_.empty()) {
            for (int i = 0; i < MAX_HANDLE; i++) {
                ProfInjectHelper::Instance().aicpuHandleCallMap_[i] = false;
            }
        }

        if (moduleId == AICORE_MODULE_ID) {
            // 使能aicore打点数据上报开关
            MsprofCommandHandle msprofCommandHandle{};
            msprofCommandHandle.type = MsprofCommandHandleType::PROF_COMMANDHANDLE_TYPE_START;
            msprofCommandHandle.profSwitch = PROF_OP_TIMESTAMP;
            uint32_t type = ProfCtrlType::PROF_CTRL_SWITCH;
            VOID_PTR data = &msprofCommandHandle;
            uint32_t len = sizeof(MsprofCommandHandle);
            handle(type, data, len);
        }
    }
}
