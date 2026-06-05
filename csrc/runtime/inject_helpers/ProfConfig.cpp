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


#include "ProfConfig.h"

#include <algorithm>
#include "include/opprof/DbiDefs.h"
#include "utils/InjectLogger.h"
#include "utils/FileSystem.h"
#include "utils/Serialize.h"
#include "core/FuncSelector.h"
#include "utils/Environment.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "InstrReport.h"
#include "MemoryContext.h"

namespace {
constexpr uint32_t MAX_NUM_PACKET = 64U;
constexpr char const *MSOPPROF_EXE_PATH_ENV = {"MSOPPROF_EXE_PATH"};
constexpr char const *CAMODEL_SOC_VERSION_ENV = {"CAMODEL_SOC_VERSION"};

template <typename T>
bool WaitForPayload(T &val)
{
    std::string rspMsg;
    T payload;
    uint32_t count = 0U;
    while (true) {
        std::string buf;
        count++;
        if (count > MAX_NUM_PACKET) {
            return false;
        }
        int len = LocalProcess::GetInstance().Wait(buf, 1);
        if (len <= 0) {
            continue;
        }
        rspMsg += buf;
        // 接收回复，期望长度为 sizeof(T)
        if (rspMsg.size() >= sizeof(T)) {
            break;
        }
    }
    bool ret = Deserialize<std::string, T>(rspMsg, payload);
    if (ret) {
        val = payload;
    }
    return ret;
}

bool WaitForReply(const std::string &objReply)
{
    std::string receivedMsg;
    int len = LocalProcess::GetInstance().Wait(receivedMsg, UINT32_MAX);
    if (len < 0) {
        return false;
    } else if (receivedMsg == objReply) {
        return true;
    }
    DEBUG_LOG("Get error reply, reply is %s", receivedMsg.c_str());
    return false;
}
}

uint64_t GetCoreNumForDbi(uint64_t blockDim)
{
    if (blockDim > MAX_BLOCK || blockDim * 3 > MAX_BLOCK) {
        WARN_LOG("Some sub block may lose dynamic instrumentation data because sub blocks exceeding %lu.", MAX_BLOCK);
        return MAX_BLOCK;
    }
    return blockDim * 3;
}

bool ProfConfig::IsEnableLogTrans() const
{
    return isCaLogTrans_;
}

void ProfConfig::SetLogTransFlag(bool transFlag)
{
    isCaLogTrans_ = transFlag;
}

void ProfConfig::RestoreMemoryByMode() const
{
    if (isAppReplay_ || isRangeReplay_) {
        return;
    }
    if (!MemoryContext::Instance().Restore()) {
        DEBUG_LOG("Failed to restore memory by kernel mode");
    }
}

void ProfConfig::RequestLogTranslate(const std::string &outputPath, const std::string &kernelName)
{
    CollectLogStart collectLogStart{};
    if (kernelName.length() >= sizeof(collectLogStart.kernelName) ||
        outputPath.length() >= sizeof(collectLogStart.outputPath)) {
        ERROR_LOG("RequestLogTranslate failed. string buffer is not enough");
        return;
    }
    std::copy(kernelName.begin(), kernelName.end(), collectLogStart.kernelName);
    std::copy(outputPath.begin(), outputPath.end(), collectLogStart.outputPath);
    ProfPacketHead head{ProfPacketType::COLLECT_START, sizeof(collectLogStart)};
    std::string msg = Serialize(head, collectLogStart);
    std::lock_guard<std::mutex> lock(communtionMx_);
    int ret = LocalProcess::GetInstance(CommType::SOCKET).Notify(msg);
    if (ret < 0 || static_cast<size_t>(ret) != msg.length()) {
        ERROR_LOG("RequestLogTranslate failed. send msg to server error ret=%d", ret);
        return;
    }

    if (WaitForReply("SUC")) {
        DEBUG_LOG("Request log translate success");
    }
}

void ProfConfig::NotifyStopTransLog()
{
    ProfPacketHead head{ProfPacketType::PROF_FINISH, 0};
    std::string msg = Serialize(head);
    std::lock_guard<std::mutex> lock(communtionMx_);
    int ret = LocalProcess::GetInstance(CommType::SOCKET).Notify(msg);
    if (ret < 0 || static_cast<size_t>(ret) != msg.length()) {
        ERROR_LOG("NotifyStopTransLog failed. send msg to server error ret=%d", ret);
        return;
    }
    if (WaitForReply("SUC")) {
        DEBUG_LOG("Stop this kernel log translate success");
    }
}

std::string ProfConfig::GetOutputPathFromRemote(const std::string &kernelName, int32_t deviceId)
{
    if (kernelName.empty()) {
        return "";
    }
    ProfDataPathConfig profDataPathConfig{};
    if (kernelName.length() < NAME_MAX_LENGTH) {
        std::copy(kernelName.begin(), kernelName.end(), profDataPathConfig.kernelName);
        profDataPathConfig.kernelName[kernelName.length()] = '\0';
    } else {
        WARN_LOG("Kernel name is too large");
        return "";
    }
    profDataPathConfig.deviceId = static_cast<uint32_t>(deviceId);
    ProfPacketHead head{ProfPacketType::DATA_PATH, sizeof(profDataPathConfig)};
    std::string msg = Serialize(head, profDataPathConfig);
    std::lock_guard<std::mutex> lock(communtionMx_);
    int ret = LocalProcess::GetInstance(CommType::SOCKET).Notify(msg);
    if (ret < 0 || static_cast<size_t>(ret) != msg.length()) {
        ERROR_LOG("GetOutputPathFromRemote failed. send msg to server error ret=%d", ret);
        return "";
    }
    std::string receivedMsg;
    uint32_t count = 0U;
    while (receivedMsg.empty() || (receivedMsg.length() > 0 && receivedMsg[receivedMsg.length() - 1] != '\n')) {
        count++;
        if (count > MAX_NUM_PACKET) {
            ERROR_LOG("GetOutputPathFromRemote failed. max packet num has been reached");
            return "";
        }
        int len = LocalProcess::GetInstance().Wait(msg, 1);
        if (len == 0) {
            continue;
        } else if (len < 0) {
            break;
        }
        receivedMsg += msg;
    }
    receivedMsg = receivedMsg.empty() ? "" : receivedMsg.substr(0, receivedMsg.length() - 1);
    return receivedMsg;
}

void ProfConfig::InitProfConfig()
{
    ProfPacketHead head{ProfPacketType::CONFIG, 0};
    std::string msg = Serialize(head);
    int ret = LocalProcess::GetInstance(CommType::SOCKET).Notify(msg);
    if (ret > 0 && static_cast<size_t>(ret) == msg.length()) {
        if (!WaitForPayload(profConfig_)) {
            ERROR_LOG("Get Init data failed");
        }
    } else {
        ERROR_LOG("Get Init data failed, request failed");
    }
}

void ProfConfig::LoadSocVersionFromEnv()
{
    if (profConfig_.isSimulator) {
        socVersion_ = GetEnv(CAMODEL_SOC_VERSION_ENV);
        if (socVersion_.empty() && !GetSocVersionFromEnvVar(socVersion_)) {
            DEBUG_LOG("Load SocVersion From Env failed");
        }
    }
}

bool ProfConfig::IsSimulatorLaunchedByDevice() const
{
    return GetEnv(DEVICE_TO_SIMULATOR) == "true";
}

ProfConfig::ProfConfig()
{
    if (IsSimulatorLaunchedByDevice()) {
        profConfig_.isSimulator = true;
    } else {
        InitProfConfig();
        isAppReplay_ = (profConfig_.replayMode == static_cast<uint8_t>(ReplayMode::APPLICATION));
        isRangeReplay_ = (profConfig_.replayMode == static_cast<uint8_t>(ReplayMode::RANGE));
    }
    LoadSocVersionFromEnv();
}

bool ProfConfig::PostNotify(ProcessCtrl::Rsp &rsp)
{
    ProcessCtrl::Req req{};
    if (!profConfig_.killAdvance) {
        return false;
    }
    req.done = 1U;
    auto currentDeviceId = DeviceContext::Local().GetRunningDeviceId();
    req.deviceId = currentDeviceId;
    ProfPacketHead head{ProfPacketType::PROCESS_CTRL, sizeof(req)};
    std::string reqMsg = Serialize(head, req);
    std::lock_guard<std::mutex> lock(communtionMx_);
    int ret = LocalProcess::GetInstance(CommType::SOCKET).Notify(reqMsg);
    if (ret < 0 || static_cast<size_t>(ret) != reqMsg.length()) {
        return false;
    }
    if (!WaitForPayload<ProcessCtrl::Rsp>(rsp)) {
        return false;
    }
    return true;
}

std::string ProfConfig::GetPluginPath(ProfDBIType pluginType) const
{
    std::string opprofPath = GetMsopprofPath();
    if (opprofPath.empty()) {
        return "";
    }
    static const std::unordered_map<ProfDBIType, std::string> pluginMap = {
        {ProfDBIType::MEMORY_CHART, "libprofplugin_memorychart.so"},
        {ProfDBIType::OPERAND_RECORD, "libprofplugin_operandrecord.so"},
        {ProfDBIType::INSTR_PROF_END, "libprofplugin_instrprofend.so"},
        {ProfDBIType::INSTR_PROF_DFX, "libprofplugin_instrprofdfx.so"},
        {ProfDBIType::INSTR_PROF_START, "libprofplugin_instrprofstart.so"},
        {ProfDBIType::WARP_TIMELINE, "libprofplugin_warptimeline.so"},
    };
    auto it = pluginMap.find(pluginType);
    if (it != pluginMap.end()) {
        return JoinPath({opprofPath, "lib64", it->second});
    }
    return "";
}

void ProfConfig::SendMsg(const std::string &msg)
{
    std::lock_guard<std::mutex> lock(communtionMx_);
    int ret = LocalProcess::GetInstance(CommType::SOCKET).Notify(msg);
    if (ret < 0 || static_cast<size_t>(ret) != msg.length()) {
        ERROR_LOG("Send one DBI data failed, error ret=%d", ret);
    }
}

std::string ProfConfig::GetMsopprofPath() const
{
    std::string msoptPath = GetEnv(MSOPPROF_EXE_PATH_ENV);
    if (!msoptPath.empty()) {
        return msoptPath;
    }
    WARN_LOG("Can not get msopt path by env.");
    std::string ascendHomePath;
    if (!GetAscendHomePath(ascendHomePath)) {
        return "";
    }
    return JoinPath({ascendHomePath, "tools", "msopt"});
}
