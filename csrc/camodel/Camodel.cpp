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
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <string>
#include "nlohmann/json.hpp"
#include "include/opprof/Protocol.h"
#include "core/PlatformConfig.h"
#include "utils/InjectLogger.h"
#include "core/FunctionLoader.h"
#include "CamodelHelper.h"
#include "CamodelRawInstrDumper.h"

#define LOAD_FUNCTION_BODY(soName, funcName, ...) \
    FUNC_BODY(soName, funcName, Origin, CAMOLDEL_ERROR_INTERNAL_ERROR, __VA_ARGS__)

constexpr int LOG_TYPE = 6;
const char *const SO_NAME = "pem_davinci";

int DvcSetLogLevelOrigin(const uint32_t filePrintLevel, const uint32_t screenPrintLevel, const uint32_t flushLevel) {
    LOAD_FUNCTION_BODY(SO_NAME, DvcSetLogLevel, filePrintLevel, screenPrintLevel, flushLevel);
}

int DvcAttachLogCallbackOrigin(DvcLogType_t logType, DvcLogCbFnUnion_t fnUnion) {
    LOAD_FUNCTION_BODY(SO_NAME, DvcAttachLogCallback, logType, fnUnion);
}

namespace {
constexpr uint32_t BAD_LOG_WARN_LIMIT = 10;

enum class BadInstrLogType : size_t {
    EMPTY_DECODE = 0,
    DECODE_TOO_LONG,
    INVALID_JSON,
    JSON_TOO_LONG,
    COUNT,
};

struct BadInstrLogCounter {
    BadInstrLogCounter() : value(0) {}
    std::atomic<uint32_t> value;
};

std::array<BadInstrLogCounter, static_cast<size_t>(BadInstrLogType::COUNT)> g_badInstrLogCounters{};

void WarnBadInstrLog(BadInstrLogType type, const char *message) {
    auto &counter = g_badInstrLogCounters[static_cast<size_t>(type)].value;
    uint32_t count = counter.fetch_add(1, std::memory_order_relaxed);
    if (count >= BAD_LOG_WARN_LIMIT) {
        return;
    }
    if (count + 1 == BAD_LOG_WARN_LIMIT) {
        WARN_LOG("%s. Further warnings of this type will be suppressed", message);
        return;
    }
    WARN_LOG("%s", message);
}

template <size_t N> bool CopyString(const std::string &source, char (&destination)[N]) {
    if (source.size() >= N) {
        return false;
    }
    std::copy_n(source.data(), source.size(), destination);
    destination[source.size()] = '\0';
    return true;
}

template <typename T> struct InstrLogAdapter;

template <> struct InstrLogAdapter<DvcInstrLogEntry_t> {
    using OutputType = Common::DvcInstrLog;

    static bool Convert(uint64_t time, const DvcInstrLogEntry_t &source, OutputType &destination) {
        if (source.decode_descr == nullptr || source.exec_descr == nullptr) {
            return false;
        }
        destination = {time, source.pc, source.core_id, source.sub_core_id};
        size_t len = std::min(std::strlen(source.decode_descr), sizeof(destination.decodeDescr) - 1);
        std::copy_n(source.decode_descr, len, destination.decodeDescr);
        destination.decodeDescr[len] = '\0';

        len = std::min(std::strlen(source.exec_descr), sizeof(destination.execDescr) - 1);
        std::copy_n(source.exec_descr, len, destination.execDescr);
        destination.execDescr[len] = '\0';
        return true;
    }
};

template <> struct InstrLogAdapter<DvcInstrLogEntryV2_t> {
    using OutputType = Common::DvcInstrLogV2;

    static bool Convert(uint64_t time, const DvcInstrLogEntryV2_t &source, OutputType &destination) {
        if (source.decode_descr == nullptr || source.decode_descr[0] == '\0') {
            WarnBadInstrLog(BadInstrLogType::EMPTY_DECODE, "Drop invalid instruction log: decode_descr is empty");
            return false;
        }

        destination.time = time;
        destination.pc = source.pc;
        destination.coreId = source.core_id;
        destination.subCoreId = source.sub_core_id;
        if (!CopyString(std::string(source.decode_descr), destination.decodeDescr)) {
            WarnBadInstrLog(BadInstrLogType::DECODE_TOO_LONG,
                "Drop invalid instruction log: decode_descr exceeds protocol capacity");
            return false;
        }

        if (source.extend_params_json == nullptr || source.extend_params_json[0] == '\0') {
            return true;
        }
        auto extendParams = nlohmann::json::parse(source.extend_params_json, nullptr, false);
        if (extendParams.is_discarded() || !extendParams.is_object()) {
            WarnBadInstrLog(
                BadInstrLogType::INVALID_JSON, "Ignore invalid extend_params_json: value is not a valid object");
            return true;
        }

        const std::string compactJson = extendParams.dump();
        if (compactJson.size() >= Common::DVC_EXTEND_PARAMS_JSON_LENGTH) {
            WarnBadInstrLog(
                BadInstrLogType::JSON_TOO_LONG, "Ignore invalid extend_params_json: value exceeds protocol capacity");
            return true;
        }

        if (!CopyString(compactJson, destination.extendParamsJson)) {
            return true;
        }
        return true;
    }
};

template <typename T> void ForwardInstrLog(uint64_t time, const T *logContent, ProfPacketType type) {
    if (!CamodelHelper::Instance().IsEnable() || logContent == nullptr) {
        return;
    }
    typename InstrLogAdapter<T>::OutputType log{};
    if (!InstrLogAdapter<T>::Convert(time, *logContent, log)) {
        return;
    }
    auto logPtr = MakeUnique<CaLogMessageHolder<typename InstrLogAdapter<T>::OutputType>>(std::move(log), type);
    CamodelHelper::Instance().SendCaLog(std::move(logPtr));
}

void ForwardInstrLogV2(uint64_t time, const DvcInstrLogEntryV2_t *logContent, ProfPacketType packetType,
    RawInstrCallbackType callbackType) {
    if (!CamodelHelper::Instance().IsEnable() || logContent == nullptr) {
        return;
    }
    Common::DvcInstrLogV2 log{};
    if (!InstrLogAdapter<DvcInstrLogEntryV2_t>::Convert(time, *logContent, log)) {
        return;
    }
    CamodelRawInstrDumper::Instance().Record(callbackType, log);
    auto logPtr = MakeUnique<CaLogMessageHolder<Common::DvcInstrLogV2>>(std::move(log), packetType);
    CamodelHelper::Instance().SendCaLog(std::move(logPtr));
}

// 定义下面的函数本身的原始动态库名
void InstrPoppedLog(uint64_t time, const DvcInstrLogEntry_t *popLog) {
    ForwardInstrLog(time, popLog, ProfPacketType::POPPED_LOG);
}

void InstrLog(uint64_t time, const DvcInstrLogEntry_t *instrLog) {
    ForwardInstrLog(time, instrLog, ProfPacketType::INSTR_LOG);
}

void InstrPoppedLogV2(uint64_t time, const DvcInstrLogEntryV2_t *popLog) {
    ForwardInstrLogV2(time, popLog, ProfPacketType::POPPED_LOG, RawInstrCallbackType::POPPED);
}

void InstrLogV2(uint64_t time, const DvcInstrLogEntryV2_t *instrLog) {
    ForwardInstrLogV2(time, instrLog, ProfPacketType::INSTR_LOG, RawInstrCallbackType::COMPLETE);
}

void MteLog(uint64_t time, const DvcMteLogEntry_t *mteLog) {
    if (!CamodelHelper::Instance().IsEnable()) {
        return;
    }
    if (mteLog == nullptr || mteLog->op == nullptr) {
        return;
    }
    Common::DvcMteLog log{};
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
    auto mteLogPtr = MakeUnique<CaLogMessageHolder<Common::DvcMteLog>>(std::move(log), ProfPacketType::MTE_LOG);
    CamodelHelper::Instance().SendCaLog(std::move(mteLogPtr));
}

void ICacheLog(uint64_t time, const DvcIcacheLogEntry_t *iCacheLog) {
    if (!CamodelHelper::Instance().IsEnable()) {
        return;
    }
    if (iCacheLog == nullptr || iCacheLog->op == nullptr) {
        return;
    }
    if (strcmp(iCacheLog->op, "miss_read") != 0 && strcmp(iCacheLog->op, "fetch_req") != 0) {
        return;
    }
    Common::DvciCacheLog log{};

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
    auto iCacheLogPtr =
        MakeUnique<CaLogMessageHolder<Common::DvciCacheLog>>(std::move(log), ProfPacketType::ICACHE_LOG);
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
        Common::DvcCcuLog log = {time, ccuLog->data.issue_success_info.pc, ccuLog->core_id, ccuLog->sub_core_id};
        auto ccuLogPtr = MakeUnique<CaLogMessageHolder<Common::DvcCcuLog>>(std::move(log), ProfPacketType::CCU_LOG);
        CamodelHelper::Instance().SendCaLog(std::move(ccuLogPtr));
    }
}

void GetSimulatorLogWithoutDump() {
    auto res = DvcSetLogLevelOrigin(6, 4, 2); // 6 ,4, 2 not save log level
    if (res == CAMOLDEL_ERROR_INTERNAL_ERROR) {
        WARN_LOG("Failed to set simulator log level, dump mode will change to on");
        return;
    }
    DvcLogCbFnUnion_t dvcLogCbFnUnion[LOG_TYPE]{};
    // This function may run from the injection library constructor before dynamically initialized
    // platform maps are ready. Use the stable SoC prefix here instead of SOC_STRING_TO_CHIP_PRODUCT.
    const std::string &socVersion = ProfConfig::Instance().GetSocVersion();
    bool isAscend950 = socVersion.compare(0, std::strlen("Ascend950"), "Ascend950") == 0;
    if (isAscend950) {
        dvcLogCbFnUnion[static_cast<uint32_t>(DvcLogType::DVC_INSTR_POPPED_LOG)].instrLogCbV2 = InstrPoppedLogV2;
        dvcLogCbFnUnion[static_cast<uint32_t>(DvcLogType::DVC_INSTR_LOG)].instrLogCbV2 = InstrLogV2;
    } else {
        dvcLogCbFnUnion[static_cast<uint32_t>(DvcLogType::DVC_INSTR_POPPED_LOG)].instrLogCb = InstrPoppedLog;
        dvcLogCbFnUnion[static_cast<uint32_t>(DvcLogType::DVC_INSTR_LOG)].instrLogCb = InstrLog;
    }
    dvcLogCbFnUnion[static_cast<uint32_t>(DvcLogType::DVC_MTE_LOG)].mteLogCb = MteLog;
    if (!isAscend950) {
        dvcLogCbFnUnion[static_cast<uint32_t>(DvcLogType::DVC_ICACHE_LOG)].icacheLogCb = ICacheLog;
        dvcLogCbFnUnion[static_cast<uint32_t>(DvcLogType::DVC_CCU_LOG)].ccuLogCb = CcuLog;
    }
    for (uint32_t i = 0; i < LOG_TYPE; i++) {
        if (!ProfConfig::Instance().IsEnablePmSampling() && i == static_cast<uint32_t>(DvcLogType::DVC_MTE_LOG)) {
            continue;
        }
        if (i == static_cast<uint32_t>(DvcLogType::DVC_IFU_LOG)) {
            continue;
        }
        if (isAscend950 &&
            (i == static_cast<uint32_t>(DvcLogType::DVC_ICACHE_LOG) ||
                i == static_cast<uint32_t>(DvcLogType::DVC_CCU_LOG))) {
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

void CamodelCtor() {
    REGISTER_LIBRARY(SO_NAME);
    REGISTER_FUNCTION(SO_NAME, DvcAttachLogCallback);
    REGISTER_FUNCTION(SO_NAME, DvcSetLogLevel);
    bool isEnableLogTrans = (GetEnv("ENABLE_CA_LOG_TRANS") == "true");
    if (isEnableLogTrans) {
        GetSimulatorLogWithoutDump();
    }
}
