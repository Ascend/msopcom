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


#include "Camodel.h"
#include <cstring>
#include <algorithm>
#include "utils/InjectLogger.h"
#include "core/FunctionLoader.h"
#include "CamodelHelper.h"

#define LOAD_FUNCTION_BODY(soName, funcName, ...)                           \
    FUNC_BODY(soName, funcName, Origin, CAMOLDEL_ERROR_INTERNAL_ERROR, __VA_ARGS__)

constexpr int LOG_TYPE = 6;
const char* const SO_NAME = "pem_davinci";

int DvcSetLogLevelOrigin(const uint32_t filePrintLevel, const uint32_t screenPrintLevel, const uint32_t flushLevel)
{
    LOAD_FUNCTION_BODY(SO_NAME, DvcSetLogLevel, filePrintLevel, screenPrintLevel, flushLevel);
}

int DvcAttachLogCallbackOrigin(DvcLogType_t logType, DvcLogCbFnUnion_t fnUnion)
{
    LOAD_FUNCTION_BODY(SO_NAME, DvcAttachLogCallback, logType, fnUnion);
}

namespace {
void InstrAndPopLog(uint64_t time, const DvcInstrLogEntry_t *logContent, ProfPacketType type)
{
    if (logContent == nullptr || logContent->decode_descr == nullptr || logContent->exec_descr == nullptr) {
        return;
    }
    DvcInstrLog log = {time, logContent->pc, logContent->core_id, logContent->sub_core_id};
    size_t len = std::min(std::strlen(logContent->decode_descr), sizeof(log.decodeDescr) - 1);
    std::copy_n(logContent->decode_descr, len, log.decodeDescr);
    log.decodeDescr[len] = '\0';

    len = std::min(std::strlen(logContent->exec_descr), sizeof(log.execDescr) - 1);
    std::copy_n(logContent->exec_descr, len, log.execDescr);
    log.execDescr[len] = '\0';

    auto popLogPtr = MakeUnique<CaLogMessageHolder<DvcInstrLog>>(std::move(log), type);
    CamodelHelper::Instance().SendCaLog(std::move(popLogPtr));
}
// 定义下面的函数本身的原始动态库名
void InstrPoppedLog(uint64_t time, const DvcInstrLogEntry_t *popLog)
{
    if (!CamodelHelper::Instance().IsEnable()) {
        return;
    }
    InstrAndPopLog(time, popLog, ProfPacketType::POPPED_LOG);
}

void InstrLog(uint64_t time, const DvcInstrLogEntry_t *instrLog)
{
    if (!CamodelHelper::Instance().IsEnable()) {
        return;
    }
    InstrAndPopLog(time, instrLog, ProfPacketType::INSTR_LOG);
}

void MteLog(uint64_t time, const DvcMteLogEntry_t *mteLog)
{
    if (!CamodelHelper::Instance().IsEnable()) {
        return;
    }
    if (mteLog == nullptr || mteLog->op == nullptr) {
        return;
    }
    DvcMteLog log{};
    std::string intf;
    if (strcmp(mteLog->op, "send_cmd") == 0 || strcmp(mteLog->op, "recv_data") == 0 ||
        strcmp(mteLog->op, "recv_cmd_rsp") == 0 || strcmp(mteLog->op, "recv_wr_data_rsp") == 0) {
        if (mteLog->data.bif_op_info.intf == nullptr) {
            return;
        }
        log = {time, mteLog->data.bif_op_info.size, mteLog->data.bif_op_info.instr_id, mteLog->core_id,
               mteLog->data.bif_op_info.req_id};
        intf = mteLog->data.bif_op_info.intf;
    } else if (strcmp(mteLog->op, "recv_rsp") == 0 || strcmp(mteLog->op, "send_req") == 0 ||
               strcmp(mteLog->op, "send_data") == 0) {
        if (mteLog->data.intf_op_info.intf == nullptr) {
            return;
        }
        log = {time, mteLog->data.intf_op_info.size, mteLog->data.intf_op_info.instr_id, mteLog->core_id,
               mteLog->data.intf_op_info.req_id};
        intf = mteLog->data.intf_op_info.intf;
    }
    size_t len = std::min(intf.length(), sizeof(log.intf) - 1);
    std::copy_n(intf.c_str(), len, log.intf);
    log.intf[len] = '\0';
    auto mteLogPtr = MakeUnique<CaLogMessageHolder<DvcMteLog>>(std::move(log), ProfPacketType::MTE_LOG);
    CamodelHelper::Instance().SendCaLog(std::move(mteLogPtr));
}

void ICacheLog(uint64_t time, const DvcIcacheLogEntry_t *iCacheLog)
{
    if (!CamodelHelper::Instance().IsEnable()) {
        return;
    }
    if (iCacheLog == nullptr || iCacheLog->op == nullptr) {
        return;
    }
    if (strcmp(iCacheLog->op, "miss_read") != 0 && strcmp(iCacheLog->op, "fetch_req") != 0) {
        return;
    }
    DvciCacheLog log{};

    if (strcmp(iCacheLog->op, "miss_read") == 0) {
        log = {time, iCacheLog->data.miss_read_info.addr, iCacheLog->core_id, iCacheLog->sub_core_id,
            iCacheLog->data.miss_read_info.size, iCacheLog->data.miss_read_info.type,
            iCacheLog->data.miss_read_info.last};
    } else if (strcmp(iCacheLog->op, "fetch_req") == 0) {
        log = {time, iCacheLog->data.fetch_req_info.addr, iCacheLog->core_id, iCacheLog->sub_core_id, 0, 0, 0};
    }
    size_t len = std::min(strlen(iCacheLog->op), sizeof(log.opType) - 1);
    std::copy_n(iCacheLog->op, len, log.opType);
    log.opType[len] = '\0';
    auto iCacheLogPtr = MakeUnique<CaLogMessageHolder<DvciCacheLog>>(std::move(log), ProfPacketType::ICACHE_LOG);
    CamodelHelper::Instance().SendCaLog(std::move(iCacheLogPtr));
}

void CcuLog(uint64_t time, const DvcCcuLogEntry_t *ccuLog) {
    if (!CamodelHelper::Instance().IsEnable()) {
        return;
    }
    if (ccuLog == nullptr || ccuLog->op == nullptr) {
        return;
    }
    if (strcmp(ccuLog->op, "ISSUE_SUCCESS") == 0) {
        DvcCcuLog log = {time, ccuLog->data.issue_success_info.pc, ccuLog->core_id, ccuLog->sub_core_id};
        auto ccuLogPtr = MakeUnique<CaLogMessageHolder<DvcCcuLog>>(std::move(log), ProfPacketType::CCU_LOG);
        CamodelHelper::Instance().SendCaLog(std::move(ccuLogPtr));
    }
}

void GetSimulatorLogWithoutDump()
{
    auto res = DvcSetLogLevelOrigin(6, 4, 2); // 6 ,4, 2 not save log level
    if (res == CAMOLDEL_ERROR_INTERNAL_ERROR) {
        WARN_LOG("Failed to set simulator log level, dump mode will change to on");
        return;
    }
    DvcLogCbFnUnion_t dvcLogCbFnUnion[LOG_TYPE];
    dvcLogCbFnUnion[static_cast<uint32_t>(DvcLogType::DVC_INSTR_POPPED_LOG)].instrLogCb = InstrPoppedLog;
    dvcLogCbFnUnion[static_cast<uint32_t>(DvcLogType::DVC_INSTR_LOG)].instrLogCb = InstrLog;
    dvcLogCbFnUnion[static_cast<uint32_t>(DvcLogType::DVC_MTE_LOG)].mteLogCb = MteLog;
    dvcLogCbFnUnion[static_cast<uint32_t>(DvcLogType::DVC_ICACHE_LOG)].icacheLogCb = ICacheLog;
    dvcLogCbFnUnion[static_cast<uint32_t>(DvcLogType::DVC_CCU_LOG)].ccuLogCb = CcuLog;
    for (uint32_t i = 0; i < LOG_TYPE; i++) {
        if (!ProfConfig::Instance().IsEnablePmSampling() && i == static_cast<uint32_t>(DvcLogType::DVC_MTE_LOG)) {
            continue;
        }
        if (i == static_cast<uint32_t>(DvcLogType::DVC_IFU_LOG)) {
            continue;
        }
        auto resCb = DvcAttachLogCallbackOrigin(static_cast<DvcLogType>(i), dvcLogCbFnUnion[i]);
        if (resCb == CAMOLDEL_ERROR_INTERNAL_ERROR) {
            WARN_LOG("Failed to get simulator log type %d", i);
        }
    }
    ProfConfig::Instance().SetLogTransFlag(true);
    DEBUG_LOG("Dlopen ca-model log translate interface success");
}
}

void CamodelCtor()
{
    REGISTER_LIBRARY(SO_NAME);
    REGISTER_FUNCTION(SO_NAME, DvcAttachLogCallback);
    REGISTER_FUNCTION(SO_NAME, DvcSetLogLevel);
    bool isEnableLogTrans = (GetEnv("ENABLE_CA_LOG_TRANS") == "true");
    if (isEnableLogTrans) {
        GetSimulatorLogWithoutDump();
    }
}
