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

// 注册回调函数给
#include "RegisterMsopprofProfileCallback.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "utils/FileSystem.h"
#include "utils/InjectLogger.h"

int32_t MsprofCompactInfoReportCallbackImpl(uint32_t agingFlag, const VOID_PTR data, uint32_t len) { return 0; }

int32_t MsprofReportAdditionalInfoCallbackImpl(uint32_t agingFlag, const VOID_PTR data, uint32_t len) {
    int32_t device = DeviceContext::GetRunningDeviceId();
    auto *hostData = static_cast<struct MsprofAdditionalInfo *>(data);
    std::string outputPath = JoinPath({GetEnv(DEVICE_PROF_DUMP_PATH_ENV), "device" + std::to_string(device)});
    if (!IsExist(outputPath) && !MkdirRecusively(outputPath)) {
        ERROR_LOG("Failed to create output path %s for aicoreTimeStamp.bin", outputPath.c_str());
        return 0;
    }
    if (outputPath.empty() || hostData == nullptr || hostData->level != MSPROF_REPORT_AIC_LEVEL ||
        hostData->type != MSPROF_REPORT_AIC_TIMESTAMP_TYPE) {
        return 0;
    }
    std::string binFile = JoinPath({outputPath, "aic_timestamp.bin"});
    if (WriteBinary(binFile, reinterpret_cast<const char *>(&hostData->data[0]), hostData->dataLen, std::ios::app) ==
        0) {
        WARN_LOG("Write bin failed, bin is %s", binFile.c_str());
    }
    return 0;
}

uint64_t MsprofGetHashIdImpl(const char *hashInfo, size_t len) { return 0; }

int8_t MsprofHostFreqIsEnableImpl() { return 0; }

int32_t MsprofApiReporterCallbackImpl(uint32_t agingFlag, const MsprofApi *const data) { return 0; }

int32_t MsprofEventReporterCallbackImpl(uint32_t agingFlag, const MsprofEvent *const event) { return 0; }

int32_t MsprofRegReportTypeInfoImpl(uint16_t level, uint32_t typeId, const char *name, size_t len) { return 0; }

int32_t MsprofDeviceStateImpl(VOID_PTR deviceState, uint32_t len) { return 0; }

RegisterMsopprofProfileCallback *RegisterMsopprofProfileCallback::Instance() {
    static RegisterMsopprofProfileCallback instance;
    return &instance;
}

void RegisterMsopprofProfileCallback::RegisterFuncMsprof() {
    if (register_) {
        return;
    }
    callBackFuncMap = {
        {static_cast<uint32_t>(ProfilerCallbackType::PROFILE_REPORT_GET_HASH_ID_C_CALLBACK),
            reinterpret_cast<VOID_PTR>(MsprofGetHashIdImpl)},
        {static_cast<uint32_t>(ProfilerCallbackType::PROFILE_HOST_FREQ_IS_ENABLE_C_CALLBACK),
            reinterpret_cast<VOID_PTR>(MsprofHostFreqIsEnableImpl)},
        {static_cast<uint32_t>(ProfilerCallbackType::PROFILE_REPORT_API_C_CALLBACK),
            reinterpret_cast<VOID_PTR>(MsprofApiReporterCallbackImpl)},
        {static_cast<uint32_t>(ProfilerCallbackType::PROFILE_REPORT_EVENT_C_CALLBACK),
            reinterpret_cast<VOID_PTR>(MsprofEventReporterCallbackImpl)},
        {static_cast<uint32_t>(ProfilerCallbackType::PROFILE_REPORT_COMPACT_CALLBACK),
            reinterpret_cast<VOID_PTR>(MsprofCompactInfoReportCallbackImpl)},
        {static_cast<uint32_t>(ProfilerCallbackType::PROFILE_REPORT_ADDITIONAL_CALLBACK),
            reinterpret_cast<VOID_PTR>(MsprofReportAdditionalInfoCallbackImpl)},
        {static_cast<uint32_t>(ProfilerCallbackType::PROFILE_REPORT_REG_TYPE_INFO_C_CALLBACK),
            reinterpret_cast<VOID_PTR>(MsprofRegReportTypeInfoImpl)},
        {static_cast<uint32_t>(ProfilerCallbackType::PROFILE_DEVICE_STATE_C_CALLBACK),
            reinterpret_cast<VOID_PTR>(MsprofDeviceStateImpl)},
    };
    for (const auto &iter : callBackFuncMap) {
        auto ret = MsprofRegisterProfileCallbackOrigin(iter.first, iter.second, sizeof(VOID_PTR));
        if (ret != MSOPPROF_FUNC_REGISTER_SUCCESS) {
            DEBUG_LOG("Failed to register reporter callback: %d", static_cast<int32_t>(iter.first));
        }
    }
    register_ = true;
}

RegisterMsopprofProfileCallback::~RegisterMsopprofProfileCallback() {
    if (register_) {
        for (const auto &iter : callBackFuncMap) {
            MsprofRegisterProfileCallbackOrigin(static_cast<uint32_t>(iter.first), nullptr, sizeof(VOID_PTR));
        }
    }
}
