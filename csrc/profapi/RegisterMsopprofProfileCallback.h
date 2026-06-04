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

#ifndef MSOPT_REGISTER_MSOPPROF_FUNC_H
#define MSOPT_REGISTER_MSOPPROF_FUNC_H

#include <map>
#include "include/thirdparty/prof.h"
#include "ProfOriginal.h"

constexpr uint16_t MSPROF_DATA_HEAD_MAGIC_NUM = 0x5a5a;
constexpr uint64_t MSPROF_EVENT_FLAG = 0xFFFFFFFFFFFFFFFFULL;

struct MsprofApi { // for MsprofReportApi
    uint16_t magicNumber = MSPROF_DATA_HEAD_MAGIC_NUM;
    uint16_t level;
    uint32_t type;
    uint32_t threadId;
    uint32_t reserve;
    uint64_t beginTime;
    uint64_t endTime;
    uint64_t itemId;
};

struct MsprofEvent { // for MsprofReportEvent
    uint16_t magicNumber = MSPROF_DATA_HEAD_MAGIC_NUM;
    uint16_t level;
    uint32_t type;
    uint32_t threadId;
    uint32_t requestId; // 0xFFFF means single event
    uint64_t timeStamp;
    uint64_t reserve = MSPROF_EVENT_FLAG;
    uint64_t itemId;
};

int32_t MsprofCompactInfoReportCallbackImpl(uint32_t agingFlag, const VOID_PTR data, uint32_t len);

int32_t MsprofReportAdditionalInfoCallbackImpl(uint32_t agingFlag, const VOID_PTR data, uint32_t len);

uint64_t MsprofGetHashIdImpl(const char *hashInfo, size_t len);

int8_t MsprofHostFreqIsEnableImpl();

int32_t MsprofApiReporterCallbackImpl(uint32_t agingFlag, const MsprofApi *const data);

int32_t MsprofEventReporterCallbackImpl(uint32_t agingFlag, const MsprofEvent *const event);

int32_t MsprofRegReportTypeInfoImpl(uint16_t level, uint32_t typeId, const char *name, size_t len);

int32_t MsprofDeviceStateImpl(VOID_PTR deviceState, uint32_t len);

class RegisterMsopprofProfileCallback {
public:
    static RegisterMsopprofProfileCallback *Instance();
    ~RegisterMsopprofProfileCallback();
    void RegisterFuncMsprof();

private:
    std::map<uint32_t, VOID_PTR> callBackFuncMap;
    bool register_ = false;
};

#endif // MSOPT_REGISTER_MSOPPROF_FUNC_H
