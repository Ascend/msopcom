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
#include "core/FuncSelector.h"
#include "ProfInjectHelper.h"
#include "include/thirdparty/prof.h"

HijackedFuncOfMsprofNotifySetDevice::HijackedFuncOfMsprofNotifySetDevice()
    : HijackedFuncType("profapi", "MsprofNotifySetDevice") {}

void HijackedFuncOfMsprofNotifySetDevice::Pre(uint32_t chipId, uint32_t deviceId, bool isOpen) {
    if (IsOpProf()) {
        // ISOPEN = TRUE的时调callback开启aicpu通道, 单卡只开启一次
        if (!isOpen) {
            return;
        }
        auto it = ProfInjectHelper::Instance().handleMap_.find(AICPU_MODULE_ID);
        if (it != ProfInjectHelper::Instance().handleMap_.end() && it->second != nullptr &&
            !ProfInjectHelper::Instance().aicpuHandleCallMap_[deviceId]) {
            MsprofCommandHandle msprofCommandHandle{};
            msprofCommandHandle.type = MsprofCommandHandleType::PROF_COMMANDHANDLE_TYPE_START;
            msprofCommandHandle.profSwitchHi = PROF_DEV_AICPU_CHANNEL;
            msprofCommandHandle.profSwitch = PROF_TASK_TIME_L1_MASK | PROF_OP_TIMESTAMP;
            msprofCommandHandle.devNums = 1;
            msprofCommandHandle.devIdList[0] = deviceId;
            msprofCommandHandle.modelId = 0;
            msprofCommandHandle.cacheFlag = 0;

            uint32_t type = ProfCtrlType::PROF_CTRL_SWITCH;
            VOID_PTR data = &msprofCommandHandle;
            uint32_t len = sizeof(MsprofCommandHandle);
            it->second(type, data, len);
            ProfInjectHelper::Instance().aicpuHandleCallMap_[deviceId] = true;
        }
    }
}
