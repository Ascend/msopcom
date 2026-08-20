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

#include "ProfDataCollect.h"

#include <map>
#include <chrono>
#include <atomic>
#include <csignal>
#include <mutex>
#include <sys/types.h>
#include <sys/wait.h>
#include <elf.h>

#include "ProfConfig.h"
#include "KernelContext.h"
#include "DeviceContext.h"
#include "ProfTask.h"
#include "utils/Ustring.h"
#include "utils/FileSystem.h"
#include "utils/Environment.h"
#include "utils/PipeCall.h"
#include "utils/signal.h"
#include "MemoryContext.h"
#include "ascend_hal/AscendHalOrigin.h"
#include "MsTx.h"
#include "BBCountDumper.h"
#include "KernelReplacement.h"
#include "ascendcl/AscendclOrigin.h"
#include "hccl/HcclOrigin.h"
#include "DBITask.h"
#include "camodel/CamodelHelper.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "profapi/ProfOriginal.h"
#include "LaunchManager.h"
#include "runtime/inject_helpers/DFXKernelLauncher.h"
#include "profapi/ProfInjectHelper.h"

using namespace std;

namespace {
constexpr uint32_t PROF_AICORE_METRICS = 0x00000004ULL;
constexpr uint32_t PROF_INSTR = 0x00800000ULL;
constexpr uint32_t PROF_L2CACHE = 0x00000010ULL;
constexpr uint32_t MSPROF_DEVICE_NUM = 1;
constexpr uint32_t PROF_INVALID_MODE_ID = 0xFFFFFFFFUL;
constexpr int32_t SYNCHRONIZE_TIME_OUT = 10000; // 设置MC2算子同步超时时间为10000ms
constexpr int32_t WAIT_DATA_READ_TIME = 100; // 设置MC2算子等待时间为100us
constexpr char const *CAMODEL_LOG_PATH_ENV = "CAMODEL_LOG_PATH";
constexpr char const *MSOPPROF_INJECTION_LIB_PATH_FROM_MSOPPROF = "lib64/libmsopprof_injection.so";
constexpr char const *MSOPPROF_OUTPUT_DUMP_PATH_ENV = "MSOPPROF_OUTPUT_DUMP_PATH";
constexpr char const *AICORE_KERNEL_NAME = "aicore_binary.o";
std::atomic<bool> g_receiveSignal{false};
std::once_flag g_sigRegFlag;
struct L2CacheClearTiling {
    uint64_t clearSizePerCore; // B
    uint32_t aicCoreNum;
};
static std::map<std::string, L2CacheClearTiling> l2CacheClearTilingMap = {
    {"Ascend910B1", {8388608, 24}},
    {"Ascend910B2", {8388608, 24}},
    {"Ascend910B2C", {8388608, 24}},
    {"Ascend910B3", {10485760, 20}},
    {"Ascend910B4", {5242880, 20}},
    {"Ascend910B4-1", {10485760, 20}},
    {"Ascend910_9391", {8388608, 24}},
    {"Ascend910_9392", {8388608, 24}},
    {"Ascend910_9381", {8388608, 24}},
    {"Ascend910_9382", {8388608, 24}},
    {"Ascend910_9372", {10485760, 20}},
    {"Ascend910_9362", {10485760, 20}},
    {"Ascend310P1", {2097152, 10}},
    {"Ascend310P3", {2097152, 8}},
    {"Ascend310P5", {2097152, 8}},
    {"Ascend310P7", {2097152, 8}},
    {"Ascend950DT_950x", {4194304, 8}},
    {"Ascend950DT_950y", {4194304, 8}},
    {"Ascend950DT_9571", {4794880, 28}},
    {"Ascend950DT_9572", {4794880, 28}},
    {"Ascend950DT_9573", {4194304, 28}},
    {"Ascend950DT_9574", {4194304, 28}},
    {"Ascend950DT_9575", {4794880, 28}},
    {"Ascend950DT_9576", {4794880, 28}},
    {"Ascend950DT_9577", {4194304, 28}},
    {"Ascend950DT_9578", {4194304, 28}},
    {"Ascend950DT_9581", {4194304, 32}},
    {"Ascend950DT_9582", {4194304, 32}},
    {"Ascend950DT_9583", {3670016, 32}},
    {"Ascend950DT_9584", {3670016, 32}},
    {"Ascend950DT_9585", {4194304, 32}},
    {"Ascend950DT_9586", {4194304, 32}},
    {"Ascend950DT_9587", {3670016, 32}},
    {"Ascend950DT_9588", {3670016, 32}},
    {"Ascend950DT_9591", {3729920, 36}},
    {"Ascend950DT_9592", {3729920, 36}},
    {"Ascend950DT_9595", {3729920, 36}},
    {"Ascend950DT_9596", {3729920, 36}},
    {"Ascend950DT_95A1", {3728280, 36}},
    {"Ascend950DT_95A2", {3728280, 36}},
    {"Ascend950PR_950z", {2097152, 8}},
    {"Ascend950PR_9579", {4794880, 28}},
    {"Ascend950PR_957b", {4194304, 28}},
    {"Ascend950PR_957c", {4194304, 28}},
    {"Ascend950PR_957d", {3596800, 28}},
    {"Ascend950PR_9589", {4194304, 32}},
    {"Ascend950PR_958b", {3670016, 32}},
    {"Ascend950PR_9599", {3729920, 36}},
};

aclError CheckAclResult(aclError result, const string &apiName) {
    if (result == ACL_SUCCESS) {
        return result;
    }
    WARN_LOG("aclrt API call %s() failed. error code: %d", apiName.c_str(), result);
    return result;
}
struct L2cacheParam {
    void *flushBuffer;
    void *buffer;
    void *cmoBuffer;
    void *stream;
    void *blockLen;
    void *l2Buffer;
    uint32_t blockDim;
    uint64_t bufferSize;
};
}

class SimulatorLauncher {
public:
    SimulatorLauncher();
    void Launch(const std::string &dumpPath, uint64_t launchId = UINT64_MAX, bool aclNew = false);

private:
    bool RuntimeToTargetLib(
        std::map<std::string, std::string> &env, const std::string &runtimePath, const std::string &targetPath) const;
    std::vector<std::string> SetEnvToSimu(const std::string &dumpPath);
    std::vector<std::string> GetLaunchArgs(const std::string &outputDir);

    std::string kernelLaunchBinPath_;
    std::string opprofInjectionLib_;
};

static void HandleSigInt(int32_t signo) {
    if (signo == SIGINT) {
        g_receiveSignal = true;
        SignalWrapper::UnregisterCallback(SIGINT);
    }
}

// DataCollect 设计为放到cpp文件中实现，不对外暴露
class DataCollect {
public:
    int32_t deviceId_ = 0;
    std::string outputPath_;
    std::string kernelName_;
    LaunchContextSP launchCtx_{nullptr};
    bool hasSimt_ = false;
    static std::mutex outputMutex_;
    static std::map<int32_t, std::string> deviceOutputPathMap_;
    static std::mutex replayCountMutex_;
    static std::map<int32_t, uint32_t> deviceReplayCountMap_;
    static std::mutex rangeConfigMutex_;
    static std::map<std::thread::id, RangeReplayConfig> threadRangeConfigMap_;
    explicit DataCollect(const LaunchContextSP &ctx, bool isInitOutput = true);
    virtual bool ProfData(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc) { return false; }
    virtual bool InstrProfData(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc) { return false; }
    virtual bool ProfData() { return false; }

    virtual void ProfInit(const void *hdl, const void *stubFunc, bool type) {};

    virtual void GenBBcountFile(uint64_t regId, uint64_t memSize, uint8_t *memInfo) {};

    virtual bool RangeReplay(const rtStream_t &stream, const aclmdlRI &modelRI) { return false; };

    bool GetPcStartAddr(const void *hdlOrStubFunc, uint64_t tiling, bool type, uint64_t &pcStart) const {
        if (type) {
            KernelContext::StubFuncPtr stubFuncPtr{hdlOrStubFunc};
            if (!KernelContext::Instance().CheckStubValid(stubFuncPtr.value) ||
                !KernelContext::Instance().GetDeviceContext().GetPcStartAddr(
                    KernelContext::StubFuncArgs{stubFuncPtr.value, nullptr}, pcStart)) {
                DEBUG_LOG("Failed to get start pc, StubFuncPtr value is %p", stubFuncPtr.value);
                return false;
            }
        } else {
            KernelContext::KernelHandlePtr hdlPtr{hdlOrStubFunc};
            if (!KernelContext::Instance().CheckHdlVaild(hdlPtr.value) ||
                !KernelContext::Instance().GetDeviceContext().GetPcStartAddr(
                    KernelContext::KernelHandleArgs{hdlPtr.value, nullptr, tiling}, pcStart)) {
                DEBUG_LOG("Failed to get start pc, KernelHandlePtr value is %p", hdlPtr.value);
                return false;
            }
        }
        return true;
    }

    virtual void GenDBIData(uint64_t memSize, uint8_t *memInfo) {};

    virtual void GenRecordData(uint64_t memSize, uint8_t *memInfo, const std::string &recordName) const {};
};
std::map<int32_t, std::string> DataCollect::deviceOutputPathMap_; // 静态成员类外初始化
std::map<int32_t, uint32_t> DataCollect::deviceReplayCountMap_;
std::map<std::thread::id, RangeReplayConfig> DataCollect::threadRangeConfigMap_;
std::mutex DataCollect::outputMutex_{};
std::mutex DataCollect::replayCountMutex_{};
std::mutex DataCollect::rangeConfigMutex_{};
class DataCollectWithSimulator : public DataCollect {
    class SharedRecord {
    public:
        static SharedRecord &Instance() {
            static SharedRecord inst;
            return inst;
        }
        const std::string &GetTmpDumpPath() {
            if (tmpDumpPath_.empty()) {
                tmpDumpPath_ = GetEnv(CAMODEL_LOG_PATH_ENV);
            }
            return tmpDumpPath_;
        }
        std::map<const KernelHandle *, std::string> binaryPathMap_;

    private:
        SharedRecord() : tmpDumpPath_(GetEnv(CAMODEL_LOG_PATH_ENV)) {}
        std::string tmpDumpPath_;
    };

public:
    virtual ~DataCollectWithSimulator() = default;
    explicit DataCollectWithSimulator(const LaunchContextSP &ctx) : DataCollect(ctx) {};
    void ProfInit(const void *hdl, const void *stubFunc, bool type) override;
    bool ProfData() override { return HandleDumpLogAfterLaunch(); }

    static bool SaveObject(const KernelHandle *hdl);
    static bool SaveObject(const RegisterContextSP &ctx);

private:
    bool MakeCaFileReadable(const std::string &filePath) const;
    void CopyAiCoreBinFile(const KernelHandle *hdl);
    void ClearCaFile(const std::string &fileName) const;
    bool HandleDumpLogAfterLaunch();
};
class DataCollectInDevice : public DataCollect {
public:
    explicit DataCollectInDevice(const LaunchContextSP &ctx, bool isInitOutput = true)
        : DataCollect(ctx, isInitOutput) {
        taskPtr_ = ProfTaskFactory::Create();
    }
    virtual ~DataCollectInDevice() = default;
    void ProfInit(const void *hdl, const void *stubFunc, bool type) override;
    bool ProfData(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc) override;
    bool InstrProfData(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc) override;
    void GenBBcountFile(uint64_t regId, uint64_t memSize, uint8_t *memInfo) override;
    void GenDBIData(uint64_t memSize, uint8_t *memInfo) override;
    void GenRecordData(uint64_t memSize, uint8_t *memInfo, const std::string &recordName) const override;
    bool RangeReplay(const rtStream_t &stream, const aclmdlRI &modelRI) override;

private:
    bool ReplayOnce(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc, L2cacheParam &param);
    bool KernelReplay(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc);
    bool KernelLaunchForInstrProf(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc);
    bool SupportProfL2CacheEvict();
    void MC2KernelLaunch(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc, bool &ret);
    void ProfCommandAction(MsprofCommandHandleType type) const;
    bool StartProf(std::thread &th);
    void StopProf() {
        if (taskPtr_ == nullptr) {
            return;
        }
        taskPtr_->Stop();
    }
    std::string KernelNameConver(const std::string &kernelName) const {
        auto start = kernelName.find("_Z");
        if (start == std::string::npos) {
            return kernelName;
        }
        uint64_t kernelNameLength = 0;
        auto end = start + 2;
        for (; end < kernelName.size(); end++) {
            if (!std::isdigit(kernelName.at(end))) {
                break;
            }
            kernelNameLength = kernelNameLength * 10 + (kernelName[end] - '0');
            if (end + 1 + kernelNameLength > kernelName.length()) { // 剩余的子串比预期的小，非法
                return kernelName;
            }
        }
        if (kernelNameLength > 0) {
            return kernelName.substr(end, kernelNameLength);
        }
        return kernelName;
    }
    bool IsPmuEventEmpty(uint32_t replayCount) const {
        std::string socVersion = DeviceContext::Local().GetSocVersion();
        auto &config = ProfConfig::Instance().GetConfig();
        ChipProductType chipType = GetProductTypeBySocVersion(socVersion);
        uint32_t pmuEventMaxNum = PMU_EVENT_MAX_NUM;
        uint32_t eventMaxNum = EVENT_MAX_NUM;
        if (IsChipSeriesTypeValid(chipType, ChipProductType::ASCEND950_SERIES)) {
            pmuEventMaxNum = PMU_EVENT_MAX_NUM_A5;
            eventMaxNum = EVENT_MAX_NUM_A5;
        }
        uint32_t nextPmuId = replayCount * pmuEventMaxNum;
        if ((nextPmuId >= eventMaxNum) || (config.aicPmu[nextPmuId] == 0 && config.aivPmu[nextPmuId] == 0)) {
            return true;
        }
        return false;
    }
    bool IsReceiveSignal() const { return g_receiveSignal; }
    void LoadFrequency();
    void SaveBasicInfo();
    void WarmUp(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc) const;
    void WarmUp(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc, uint16_t warmUpTimes) const;
    void WarmUp(const rtStream_t &stream, const aclmdlRI &modelRI) const;
    bool PrepareClearL2CacheParam(L2cacheParam &param) const;
    bool MallocBuffer(L2cacheParam &param) const;
    bool WaitClearL2Cache(L2cacheParam &param) const;
    void FreeL2Cache(L2cacheParam &param) const;
    bool ClearL2Cache(L2cacheParam &param) const;
    bool CallEmptyKernel(void *stream) const;
    size_t GetReplayTimes() const { return static_cast<size_t>(EVENT_MAX_NUM / PMU_EVENT_MAX_NUM); }
    bool RangeReplayProfData(const rtStream_t &stream);
    bool RangeReplayInit(bool &needReplay);
    void SaveFrequency(const string &outputPath) const;
    bool RangeReplayOnce(L2cacheParam &param, aclmdlRI const &modelRI);

    SimulatorLauncher simulatorLauncher;
    std::thread readThread_;
    std::atomic<uint32_t> replayCount_{0};
    int32_t curFreq_{-1};
    int64_t ratedFreq_{-1};
    std::unique_ptr<ProfTask> taskPtr_;
    bool isClearParamSuccess_{false};
    bool profL2cacheEvict_ = false;
};

DataCollect::DataCollect(const LaunchContextSP &ctx, bool isInitOutput) {
    if (ctx != nullptr &&
        ctx->GetFuncContext()->GetRegisterContext()->GetBinaryType() ==
            aclrtBinaryLoadOptionType::ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE) {
        outputPath_ = "";
        DEBUG_LOG("ship aicpu kernel launch");
        return;
    }
    deviceId_ = DeviceContext::GetRunningDeviceId();
    int32_t devId = ProfConfig::Instance().IsSimulator() ? 0 : deviceId_;
    if (!isInitOutput) {
        return;
    }
    constexpr uint32_t unixFileNameLimit = 255; // unix file name max limits is 255
    constexpr uint32_t kernelNameReserved = 72; // too long kernel name reserved 72
    if (MsTx::Instance().IsInMstxRange()) {
        if (ctx == nullptr) {
            kernelName_ = KernelContext::Instance().GetLaunchName();
        } else {
            launchCtx_ = ctx;
            kernelName_ = launchCtx_->GetFuncContext() != nullptr ? launchCtx_->GetFuncContext()->GetKernelName() : "";
        }
    }
    std::string kernelName(kernelName_);
    // if output path too long,hash kernel name
    if (kernelName.size() >= unixFileNameLimit) {
        // hash kernel name
        size_t hashLen = kernelName.size() - kernelNameReserved;
        auto hashStr = kernelName.substr(kernelNameReserved, hashLen);
        std::hash<std::string> hashFunc;
        size_t hashValue = hashFunc(hashStr);
        kernelName = kernelName.substr(0, kernelNameReserved) + "_" + std::to_string(hashValue);
    }
    hasSimt_ = launchCtx_ != nullptr ? launchCtx_->GetFuncContext()->GetRegisterContext()->HasSimtSymbol()
                                     : KernelContext::Instance().HasSimtSymbol();
    if (ProfConfig::Instance().IsSimulatorLaunchedByDevice()) {
        outputPath_ = GetEnv(MSOPPROF_OUTPUT_DUMP_PATH_ENV);
    } else {
        outputPath_ = ProfConfig::Instance().GetOutputPathFromRemote(kernelName, devId);
        std::lock_guard<std::mutex> lk(outputMutex_);
        deviceOutputPathMap_[devId] = outputPath_;
    }
    if (!outputPath_.empty() && !MkdirRecusively(outputPath_)) {
        WARN_LOG("Output path create failed, path is: %s", outputPath_.c_str());
    }
}

void SimulatorLauncher::Launch(const std::string &dumpPath, uint64_t launchId, bool aclNew) {
    std::string outputDir = JoinPath({dumpPath, "kernel_data"});
    if (!KernelDumper::Instance().Dump(outputDir, launchId, aclNew)) {
        WARN_LOG("Msopt dump kernel failed");
        return;
    }
    std::vector<std::string> envs = SetEnvToSimu(dumpPath);
    std::vector<std::string> args = GetLaunchArgs(outputDir);
    std::vector<char *> argumentsOutput = ToRawCArgv(args);
    const pid_t pid{fork()};
    if (pid < 0) {
        WARN_LOG("Fork kernel-launcher process failed");
    } else if (pid == 0) {
        execvpe(kernelLaunchBinPath_.c_str(), argumentsOutput.data(), ToRawCArgv(envs).data());
        _exit(EXIT_FAILURE);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (status != 0) {
            WARN_LOG("Child process exited with status %d", status);
        }
    }
}

bool SimulatorLauncher::RuntimeToTargetLib(
    std::map<std::string, std::string> &env, const std::string &runtimePath, const std::string &targetPath) const

{
    if (runtimePath.empty() || !MkdirRecusively(runtimePath)) {
        ERROR_LOG("Sym link failed when dispose ca path");
        return false;
    }
    std::string soName = JoinPath({runtimePath, "libruntime.so"});
    std::string ldEnv = env["LD_LIBRARY_PATH"];
    if (IsExist(soName)) {
        RemoveAll(soName);
    }
    if (symlink(targetPath.c_str(), soName.c_str()) != 0) {
        ERROR_LOG("Symbol link runtime to simulator failed");
        return false;
    }
    env["LD_LIBRARY_PATH"] = runtimePath;
    if (!ldEnv.empty()) {
        env["LD_LIBRARY_PATH"] += ":" + ldEnv;
    }
    DEBUG_LOG("Symbol link runtime to simulator success, so path is %s, simulator path is %s", soName.c_str(),
        targetPath.c_str());
    return true;
}

std::vector<std::string> SimulatorLauncher::SetEnvToSimu(const std::string &dumpPath) {
    std::map<std::string, std::string> env;
    std::string camodelPath = dumpPath;
    // outputPath is OPPOxxx/device0/kernelName/0/dump, and CAMODEL_LOG_PATH should in OPPOxxx/device0/tmp_dump
    // so CAMODEL_LOG_PATH should rollback 3 times and concatenate "tmp_dump"
    if (!RollbackPath(camodelPath, 3)) {
        return {};
    }
    camodelPath = JoinPath({camodelPath, "tmp_dump"});
    env[MSOPPROF_OUTPUT_DUMP_PATH_ENV] = dumpPath;
    env[CAMODEL_LOG_PATH_ENV] = camodelPath;
    env[DEVICE_TO_SIMULATOR] = "true";
    env[IS_SIMULATOR_ENV] = "true";
    env["LD_PRELOAD"] = opprofInjectionLib_ + ":libruntime_camodel.so";
    MkdirRecusively(camodelPath);
    std::vector<std::string> outEnvs;
    RuntimeToTargetLib(env, env[CAMODEL_LOG_PATH_ENV], opprofInjectionLib_);
    JoinWithSystemEnv(env, outEnvs, true);
    return outEnvs;
}

std::vector<std::string> SimulatorLauncher::GetLaunchArgs(const std::string &outputDir) {
    std::string binFilePath = JoinPath({outputDir, KERNEL_CONFIG_NAME});
    std::vector<std::string> kernelLaunchArgs;
    kernelLaunchArgs.emplace_back(kernelLaunchBinPath_);
    kernelLaunchArgs.emplace_back("-c");
    kernelLaunchArgs.emplace_back(binFilePath);
    return kernelLaunchArgs;
}

SimulatorLauncher::SimulatorLauncher() {
    std::string opprofPath = ProfConfig::Instance().GetMsopprofPath();
    if (!opprofPath.empty()) {
        opprofInjectionLib_ = JoinPath({opprofPath, MSOPPROF_INJECTION_LIB_PATH_FROM_MSOPPROF});
        kernelLaunchBinPath_ = JoinPath({opprofPath, "bin", "kernel-launcher"});
        // 验证可执行文件是否存在且可执行
        if (!IsExist(kernelLaunchBinPath_) || !IsExecutable(kernelLaunchBinPath_)) {
            ERROR_LOG("Kernel launcher not found or not executable: %s", kernelLaunchBinPath_.c_str());
        }
    }
}

void DataCollectWithSimulator::ProfInit(const void *hdl, const void *stubFunc, bool type) {
    DEBUG_LOG("Kernel running, kernel name is %s", kernelName_.c_str());
    if (outputPath_.empty()) {
        CamodelHelper::Instance().Disable();
        return;
    }
    INFO_LOG("Start profiling on kernel: %s", kernelName_.c_str());
    if (!MkdirRecusively(outputPath_)) {
        WARN_LOG("Output path create failed, path is: %s", outputPath_.c_str());
        outputPath_ = "";
        return;
    }
    if (launchCtx_ != nullptr) {
        CopyAiCoreBinFile(launchCtx_->GetFuncContext()->GetRegisterContext()->GetHandle());
    } else {
        CopyAiCoreBinFile(hdl);
    }
    std::string basicInfoTxt = JoinPath({SharedRecord::Instance().GetTmpDumpPath(), "object_dump.txt"});
    WriteStringToFile(basicInfoTxt, outputPath_ + '\n' + kernelName_);
    uint64_t tiling = 0;
    uint64_t pcStart = 0;
    if (launchCtx_ != nullptr) {
        pcStart = launchCtx_->GetFuncContext()->GetStartPC();
    } else {
        KernelContext::LaunchEvent event;
        if (KernelContext::Instance().GetLastLaunchEvent(event)) {
            tiling = event.tilingKey;
        }
        if (type && !GetPcStartAddr(stubFunc, tiling, type, pcStart)) {
            DEBUG_LOG("Get pc start failed by stubFunc. Using pc start in dump log");
        }
        if (!type && !GetPcStartAddr(hdl, tiling, type, pcStart)) {
            DEBUG_LOG("Get pc start failed by hdl. Using pc start in dump log");
        }
    }
    if (pcStart != 0) {
        WriteStringToFile(JoinPath({outputPath_, "pc_start_addr.txt"}), NumToHexString(pcStart),
            std::fstream::out | std::fstream::binary);
    }
    // 需要先复制aicore.o到dump下
    if (GetEnv("ENABLE_CA_LOG_TRANS") == "true") {
        if (ProfConfig::Instance().IsEnableLogTrans()) {
            CamodelHelper::Instance().Enable();
            ProfConfig::Instance().RequestLogTranslate(outputPath_, kernelName_);
        } else {
            ProfConfig::Instance().RequestLogTranslate("", "");
        }
    }
}

bool DataCollectWithSimulator::MakeCaFileReadable(const std::string &filePath) const {
    if (IsWritable(filePath)) {
        return true;
    }
    std::size_t end = filePath.find_last_of('.');
    std::size_t start = filePath.rfind('.', end - 1);
    if (start != std::string::npos && end != std::string::npos) {
        std::string tempFile = filePath.substr(start + 1, end - start - 1);
        if (!IsDigit(tempFile)) {
            return true;
        }
        if (!Chmod(filePath, SAVE_DATA_FILE_AUTHORITY)) {
            return false;
        }
        DEBUG_LOG("Change file permission, file name is %s", filePath.c_str());
    }
    return true;
}

void DataCollectWithSimulator::CopyAiCoreBinFile(const KernelHandle *hdl) {
    using namespace std::experimental::filesystem;
    auto iter = SharedRecord::Instance().binaryPathMap_.find(hdl);
    if (iter == SharedRecord::Instance().binaryPathMap_.end()) {
        WARN_LOG("Copy aicore bin file failed");
        return;
    }
    std::string tempPath = iter->second;
    if (IsExist(JoinPath({outputPath_, AICORE_KERNEL_NAME}))) {
        DEBUG_LOG("Aicore kernel already in out put path");
        return;
    }
    if (!IsExist(tempPath)) {
        WARN_LOG("Copy aicore bin file failed, tempPath is %s", tempPath.c_str());
        return;
    }
    CopyFile(tempPath, outputPath_);
}

void DataCollectWithSimulator::ClearCaFile(const std::string &fileName) const {
    std::string realPath = fileName;
    if (!CheckWriteFilePathValid(realPath)) {
        WARN_LOG("check file path %s failed", realPath.c_str());
        return;
    }
    if (!MakeCaFileReadable(realPath)) {
        return;
    }
    std::ofstream file(realPath, std::ios::trunc);
    if (!file.is_open()) {
        WARN_LOG("Can not find dump file path [%s]", realPath.c_str());
        return;
    }
    file.close();
}

bool DataCollectWithSimulator::HandleDumpLogAfterLaunch() {
    using namespace std::experimental::filesystem;
    if (ProfConfig::Instance().IsEnableLogTrans() && CamodelHelper::Instance().IsEnable()) {
        CamodelHelper::Instance().SendSync();
        ProfConfig::Instance().NotifyStopTransLog();
    }
    std::string tmpDumpPath = SharedRecord::Instance().GetTmpDumpPath();
    if (tmpDumpPath.empty() || !IsExist(tmpDumpPath)) {
        WARN_LOG("Tmp dump file path is not Exist, path is [%s]", tmpDumpPath.c_str());
        return false;
    }
    if (!outputPath_.empty() && IsExist(outputPath_)) {
        for (auto const &dirEntry : directory_iterator(tmpDumpPath)) {
            if (IsDir(dirEntry.path()) || IsSoftLink(dirEntry.path())) {
                continue;
            }
            std::string fileName = dirEntry.path().filename().string();
            if (fileName.find("dump") == std::string::npos) {
                continue;
            }
            bool retCopy = CopyFile(dirEntry.path().string(), outputPath_);
            if (retCopy) {
                std::string dstFilePath = JoinPath({outputPath_, dirEntry.path().filename().string()});
                Chmod(dstFilePath, SAVE_DATA_FILE_AUTHORITY);
            }
            ClearCaFile(dirEntry.path().string());
        }
        return true;
    }
    for (auto const &dirEntry : directory_iterator(tmpDumpPath)) {
        if (IsDir(dirEntry.path()) || IsSoftLink(dirEntry.path())) {
            continue;
        }
        ClearCaFile(dirEntry.path().string());
    }
    return true;
}

bool DataCollectWithSimulator::SaveObject(const KernelHandle *hdl) {
    if (SharedRecord::Instance().binaryPathMap_.count(hdl) != 0) {
        return true;
    }
    std::string objFileName = GenerateTimeStamp<std::chrono::nanoseconds>();
    std::string outputDir = JoinPath({SharedRecord::Instance().GetTmpDumpPath(), objFileName});
    if (KernelContext::Instance().DumpKernelObject(hdl, outputDir, AICORE_KERNEL_NAME)) {
        SharedRecord::Instance().binaryPathMap_[hdl] = JoinPath({outputDir, AICORE_KERNEL_NAME});
        return true;
    }
    return false;
}

bool DataCollectWithSimulator::SaveObject(const RegisterContextSP &ctx) {
    auto hdl = ctx->GetHandle();
    if (SharedRecord::Instance().binaryPathMap_.count(hdl) != 0) {
        return true;
    }
    std::string objFileName = GenerateTimeStamp<std::chrono::nanoseconds>();
    std::string outputDir = JoinPath({SharedRecord::Instance().GetTmpDumpPath(), objFileName});
    std::string outputPath = JoinPath({outputDir, AICORE_KERNEL_NAME});
    if (!IsExist(outputDir) && !MkdirRecusively(outputDir)) {
        WARN_LOG("Save binary file failed, cannot create output dir");
        return false;
    }
    if (ctx->Save(outputPath)) {
        SharedRecord::Instance().binaryPathMap_[hdl] = outputPath;
        return true;
    }
    WARN_LOG("Can not save binary file to %s", outputPath.c_str());
    return false;
}

void DataCollectInDevice::ProfInit(const void *hdl, const void *stubFunc, bool type) {
    if (outputPath_.empty()) {
        return;
    }
    // set flag if operator is lccl or mc2
    std::map<std::string, Elf64_Shdr> headers;
    uint64_t tiling = 0;
    uint64_t pcStart = 0;
    if (launchCtx_ != nullptr) {
        // Get section headers
        const std::vector<char> &binary = launchCtx_->GetFuncContext()->GetRegisterContext()->GetElfData();
        GetSectionHeaders(binary, headers);

        // Dump obj file
        std::string outputPath = JoinPath({outputPath_, AICORE_KERNEL_NAME});
        if (!launchCtx_->GetFuncContext()->GetRegisterContext()->Save(outputPath)) {
            WARN_LOG("Save obj failed, output path is %s", outputPath_.c_str());
        }
        // Get pc start
        pcStart = launchCtx_->GetFuncContext()->GetStartPC();
    } else {
        // Get section headers
        rtDevBinary_t binary;
        KernelContext::Instance().GetDevBinary(KernelContext::KernelHandlePtr{hdl}, binary) &&
            GetSectionHeaders(binary, headers);

        // Dump obj file
        KernelDumper::Instance().DumpAicore(outputPath_);

        // Get pc start
        KernelContext::LaunchEvent event;
        if (KernelContext::Instance().GetLastLaunchEvent(event)) {
            tiling = event.tilingKey;
        }
        if (type && !GetPcStartAddr(stubFunc, tiling, type, pcStart)) {
            DEBUG_LOG("Get pc start failed by stubFunc. Using pc start in dump log");
        }
        if (!type && !GetPcStartAddr(hdl, tiling, type, pcStart)) {
            DEBUG_LOG("Get pc start failed by hdl. Using pc start in dump log");
        }
    }
    for (const auto &h : headers) {
        if (h.first == "Attr_Section_Lcal") {
            KernelContext::Instance().SetLcclFlag(true);
            DEBUG_LOG("set lccl flag");
            break;
        }
    }
    KernelContext::Instance().SetMC2Flag();
    if (pcStart != 0) {
        WriteStringToFile(JoinPath({outputPath_, "pc_start_addr.txt"}), NumToHexString(pcStart),
            std::fstream::out | std::fstream::binary);
    }
}

void DataCollectInDevice::ProfCommandAction(MsprofCommandHandleType type) const {
    using RtCommandHandleParamsT = struct {
        uint32_t pathLen;
        uint32_t storageLimit; // MB
        uint32_t profDataLen;
        char path[PARAM_LEN_MAX + 1];
        char profData[PATH_LEN_MAX + 1];
    };
    using RtProfCommandHandleT = struct {
        uint64_t profSwitch;
        uint64_t profSwitchHi;
        uint32_t devNums;
        uint32_t devIdList[MSPROF_MAX_DEV_NUM];
        uint32_t modelId;
        uint32_t type;
        uint32_t cacheFlag;
        RtCommandHandleParamsT commandHandleParams;
    };
    RtProfCommandHandleT command;
    command.type = static_cast<uint32_t>(type);
    command.devNums = MSPROF_DEVICE_NUM;
    command.devIdList[0] = static_cast<uint32_t>(deviceId_);
    command.modelId = PROF_INVALID_MODE_ID;
    auto res = ACL_SUCCESS;
    std::string socVersion = DeviceContext::Local().GetSocVersion();
    auto chipType = GetProductTypeBySocVersion(socVersion);
    if (IsChipSeriesTypeValid(chipType, ChipProductType::ASCEND310P_SERIES)) {
        command.profSwitch = PROF_AICORE_METRICS;
    } else if (IsChipSeriesTypeValid(chipType, ChipProductType::ASCEND910B_SERIES) ||
        IsChipSeriesTypeValid(chipType, ChipProductType::ASCEND910_93_SERIES)) {
        command.profSwitch = PROF_L2CACHE | PROF_OP_TIMESTAMP;
    } else if (IsChipSeriesTypeValid(chipType, ChipProductType::ASCEND950_SERIES)) {
        if (ProfConfig::Instance().IsInstrTimelineEnabled() || ProfConfig::Instance().IsPipeTimelineEnabled() ||
            (ProfConfig::Instance().IsPCSamplingEnabled() && hasSimt_)) {
            command.profSwitch = PROF_INSTR | PROF_OP_TIMESTAMP;
        } else {
            command.profSwitch = PROF_OP_TIMESTAMP;
        }
    }
    res = profSetProfCommandOrigin(static_cast<void *>(&command), sizeof(RtProfCommandHandleT));
    DEBUG_LOG("profSetProfCommandOrigin type %d res is %d", static_cast<int>(type), static_cast<int>(res));
}

bool DataCollectInDevice::StartProf(std::thread &th) {
    std::call_once(g_sigRegFlag, [&]() { SignalWrapper::RegisterCallback(SIGINT, HandleSigInt); });
    DEBUG_LOG("MSOPT INJECTION SUCCESS");
    if (!taskPtr_->Start(replayCount_, hasSimt_)) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(DataCollect::replayCountMutex_);
        deviceReplayCountMap_[deviceId_] = replayCount_;
    }
    th = std::thread(&ProfTask::ChannelRead, taskPtr_.get());
    DEBUG_LOG("Polling data read thread create");
    return true;
}

void DataCollectInDevice::GenBBcountFile(uint64_t regId, uint64_t memSize, uint8_t *memInfo) {
    if (!IsPlatformSupportDBI()) {
        DEBUG_LOG("Unsupported platform, exit DBI");
        return;
    }
    std::string storePath = GetEnv(DEVICE_PROF_DUMP_PATH_ENV);
    if (storePath.empty() || !IsExist(storePath)) {
        WARN_LOG("Error in device dump path");
        return;
    }
    std::string extraName = BBCountDumper::Instance().GenExtraAndReturnName(outputPath_, regId, memSize, memInfo);
    if (extraName.empty()) {
        WARN_LOG("Extra BB count file dump failed, memsize is %lu", memSize);
        return;
    }
    std::string kernelFile = JoinPath({outputPath_, "kernel" + std::to_string(regId) + ".o"});
    std::string newKernelFile = JoinPath({outputPath_, AICORE_KERNEL_NAME});
    if (!IsExist(kernelFile) || rename(kernelFile.c_str(), newKernelFile.c_str()) != 0) {
        WARN_LOG("Rename aicore file failed");
    }
}

void DataCollectInDevice::GenDBIData(uint64_t memSize, uint8_t *memInfo) {
    DEBUG_LOG("Gen DBI data, memSize is %lu", memSize);
    if (memSize == 0) {
        return;
    }
    std::vector<uint8_t> memInfoHost(MAX_BLOCK_DATA_SIZE);
    uint64_t count = memSize / BLOCK_MEM_SIZE;
    for (uint64_t i = 0; i < count; ++i) {
        aclError error = aclrtMemcpyImplOrigin(memInfoHost.data(), MAX_BLOCK_DATA_SIZE, memInfo + i * BLOCK_MEM_SIZE,
            MAX_BLOCK_DATA_SIZE, aclrtMemcpyKind::ACL_MEMCPY_DEVICE_TO_HOST);
        if (error != RT_ERROR_NONE) {
            ERROR_LOG("dump basic block data rtMemcpy memInfo error: %d", error);
            break;
        }
        auto bh = reinterpret_cast<const BlockHeader *>(memInfoHost.data());
        uint64_t length = bh->length;
        if (length == 0) {
            continue;
        }
        uint64_t overflow = 0;
        if ((bh->length & RECORD_OVERFLOW_BIT) != 0) {
            if (((bh->length ^ RECORD_OVERFLOW_BIT) == bh->count) || ((bh->length ^ RECORD_OVERFLOW_BIT) < bh->count)) {
                continue;
            }
            overflow = (bh->length ^ RECORD_OVERFLOW_BIT) - bh->count;
            length = MAX_BLOCK_DATA_SIZE - sizeof(BlockHeader);
        }
        DBIDataHeader dbiDataHeader{bh->count, length, overflow, static_cast<uint16_t>(i), 0};
        ProfPacketHead head{ProfPacketType::DBI_DATA, static_cast<uint32_t>(length + sizeof(dbiDataHeader))};
        std::string msg = Serialize(head, dbiDataHeader);
        auto begin = memInfoHost.begin() + sizeof(BlockHeader);
        msg.insert(msg.end(), begin, begin + static_cast<long>(length));
        ProfConfig::Instance().SendMsg(msg);
    }
    DBIDataHeader dbiHeader{0, outputPath_.size(), 0, 0, 1};
    ProfPacketHead head{ProfPacketType::DBI_DATA, static_cast<uint32_t>(dbiHeader.length + sizeof(dbiHeader))};
    ProfConfig::Instance().SendMsg(Serialize(head, dbiHeader) + outputPath_);
}

void DataCollectInDevice::GenRecordData(uint64_t memSize, uint8_t *memInfo, const std::string &recordName) const {
    DEBUG_LOG("Gen record %s, memSize is %lu", recordName.c_str(), memSize);
    if (memSize == 0) {
        return;
    }
    void *memInfoHost;
    if (aclrtMallocHostImplOrigin(&memInfoHost, memSize) != ACL_SUCCESS) {
        ERROR_LOG("dump basic block data aclrtMallocHost");
        return;
    }
    aclError error =
        aclrtMemcpyImplOrigin(memInfoHost, memSize, memInfo, memSize, aclrtMemcpyKind::ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_SUCCESS) {
        ERROR_LOG("dump basic block data aclrtMemcpy memInfo error: %d", error);
        aclrtFreeHostImplOrigin(memInfoHost);
        memInfoHost = nullptr;
        return;
    }
    auto path = JoinPath({outputPath_, recordName});
    if (WriteBinary(path, reinterpret_cast<const char *>(memInfoHost), memSize) == 0) {
        WARN_LOG("Write %s failed", recordName.c_str());
    }
    aclrtFreeHostImplOrigin(memInfoHost);
}

void DataCollectInDevice::MC2KernelLaunch(
    rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc, bool &ret) {
    ret = false;
    auto hcclComm = KernelContext::Instance().GetHcclComm();
    if (hcclComm == nullptr) {
        WARN_LOG("Get hccl comm failed, device: %d", deviceId_);
        return;
    }
    // use event to make aicpu stream wait aicore stream
    aclrtEvent event = nullptr;
    int aclrtRes = ACL_ERROR_NONE;
    aclrtRes = aclrtCreateEventOrigin(&event);
    if (aclrtRes != ACL_ERROR_NONE) {
        WARN_LOG("MC2 event create failed, device: %d, res: %d", deviceId_, aclrtRes);
        return;
    }

    std::shared_ptr<void> deferA(nullptr, [&event](std::nullptr_t &) { aclrtDestroyEventOrigin(event); });

    HcclResult hcclRes = HcclBarrierOrigin(hcclComm, stream);
    if (hcclRes != HCCL_SUCCESS) {
        WARN_LOG("Call hccl barrier failed, device: %d, res: %d", deviceId_, hcclRes);
        return;
    }
    aclrtRes = aclrtRecordEventOrigin(event, stream);
    if (aclrtRes != ACL_ERROR_NONE) {
        WARN_LOG("MC2 event record failed, device: %d, res: %d", deviceId_, aclrtRes);
        return;
    }
    AicpuLaunchArgs &aicpuLaunchArgs = KernelContext::GetAicpuLaunchArgs();
    aclrtRes = aclrtStreamWaitEventOrigin(aicpuLaunchArgs.stm, event);
    if (aclrtRes != ACL_ERROR_NONE) {
        WARN_LOG("MC2 event wait failed, device: %d, res: %d", deviceId_, aclrtRes);
        return;
    }

    rtError_t aicpuRet = rtAicpuKernelLaunchExWithArgsOrigin(aicpuLaunchArgs.kernelType, &aicpuLaunchArgs.opName[0],
        aicpuLaunchArgs.blockDim, aicpuLaunchArgs.argsInfo, aicpuLaunchArgs.smDesc, aicpuLaunchArgs.stm,
        aicpuLaunchArgs.flags);
    ret = kernelLaunchFunc(); // mc2 aicore
    LoadFrequency();
    aclError npuSyncRet{ACL_SUCCESS};
    if (ret) {
        npuSyncRet = aclrtSynchronizeStreamWithTimeoutImplOrigin(stream, SYNCHRONIZE_TIME_OUT);
        DEBUG_LOG("MC2 AICore synchronize, device: %d, res: %d", deviceId_, npuSyncRet);
    }
    aclError cpuSyncRet{ACL_SUCCESS};
    if (aicpuRet == RT_ERROR_NONE) {
        cpuSyncRet = aclrtSynchronizeStreamWithTimeoutImplOrigin(aicpuLaunchArgs.stm, SYNCHRONIZE_TIME_OUT);
        DEBUG_LOG("MC2 AICPU synchronize, device: %d, res: %d", deviceId_, cpuSyncRet);
    }
    if (cpuSyncRet != ACL_SUCCESS) {
        WARN_LOG("AICPU execute failed, device: %d, res: %d", deviceId_, aicpuRet);
    }
    aclrtRes = aclrtResetEventOrigin(event, aicpuLaunchArgs.stm);
    if (aclrtRes != ACL_ERROR_NONE) {
        WARN_LOG("MC2 event reset failed, device: %d, res: %d", deviceId_, aclrtRes);
    }
}

bool DataCollectInDevice::ReplayOnce(
    rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc, L2cacheParam &param) {
    bool isMC2 = KernelContext::Instance().GetMC2Flag();
    uint16_t warmUpTimes = ProfConfig::Instance().GetWarmUpTimes();
    if (IsPmuEventEmpty(replayCount_) && replayCount_ != 0) {
        DEBUG_LOG("Skip profiling because pmu is empty");
        return true;
    }
    DEBUG_LOG("Replaying round on device %d No. %d time", deviceId_, replayCount_ + 1);
    if (!ProfConfig::Instance().IsAppReplay() && isClearParamSuccess_ && !ClearL2Cache(param)) {
        WARN_LOG("Clear L2Cache failed. replay count is %d", replayCount_ + 1);
    }
    if (!isMC2 && !ProfConfig::Instance().IsAppReplay() && !KernelContext::Instance().GetLcclFlag() &&
        !MemoryContext::Instance().Restore()) {
        WARN_LOG("Replay data restore failed. Skipping %d", replayCount_ + 1);
        return false;
    }
    if (!StartProf(readThread_)) {
        StopProf();
        WARN_LOG("Start profiling failed. Skipping %d", replayCount_ + 1);
        return false;
    }
    if (profL2cacheEvict_ && CallEmptyKernel(stream) == ACL_SUCCESS) {
        DEBUG_LOG("Success run empty kernel for l2cache");
    }
    bool ret = true;
    if (KernelContext::Instance().GetLcclFlag() && !profL2cacheEvict_) {
        for (uint16_t i = 0; i < warmUpTimes; ++i) {
            kernelLaunchFunc();
        }
    }
    aclError syncRet{ACL_SUCCESS};
    if (isMC2) {
        MC2KernelLaunch(stream, kernelLaunchFunc, ret);
    } else {
        ret = kernelLaunchFunc();
        if (IsPmuEventEmpty(replayCount_ + 1)) {
            LoadFrequency();
        }
        if (ret) {
            syncRet = aclrtSynchronizeStreamWithTimeoutImplOrigin(stream, SYNCHRONIZE_TIME_OUT);
        }
    }

    usleep(WAIT_DATA_READ_TIME); // 延时100us，防止duration数据读取不全
    StopProf();
    if (readThread_.joinable()) {
        readThread_.join();
    }
    if (syncRet == ACL_SUCCESS && ret) {
        return true;
    } else {
        WARN_LOG("Kernel run on device %d No. %d time failed.", deviceId_, replayCount_ + 1);
    }
    return ret;
}

bool DataCollectInDevice::KernelLaunchForInstrProf(
    rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc) {
    uint16_t warmUpTimes = ProfConfig::Instance().GetWarmUpTimes();
    WarmUp(stream, kernelLaunchFunc, warmUpTimes);
    ProfCommandAction(MsprofCommandHandleType::PROF_COMMANDHANDLE_TYPE_START);

    bool isMC2 = KernelContext::Instance().GetMC2Flag();
    std::thread readThread;
    if (!StartProf(readThread)) {
        StopProf();
        WARN_LOG("Start profiling failed. Skipping KernelLaunchForInstrProf");
        return false;
    }
    bool ret = true;
    aclError syncRet{ACL_SUCCESS};
    if (isMC2) {
        MC2KernelLaunch(stream, kernelLaunchFunc, ret);
    } else {
        ret = kernelLaunchFunc();
        if (ret) {
            syncRet = aclrtSynchronizeStreamWithTimeoutImplOrigin(stream, SYNCHRONIZE_TIME_OUT);
        }
    }
    StopProf();
    if (readThread.joinable()) {
        readThread.join();
    }
    if (syncRet == ACL_SUCCESS && ret) {
        return ret;
    } else {
        WARN_LOG("Kernel run on device %d for instr prof failed.", deviceId_);
    }
    if (ProfConfig::Instance().IsAppReplay()) {
        return ret;
    }
    // MC2调优去掉Restore,配合取消AICORE 劫持函数CALL的那次调用，取得最后一次重放的aicore的timeline
    if (!isMC2 && !KernelContext::Instance().GetLcclFlag()) {
        MemoryContext::Instance().Restore();
    }
    return ret;
}

bool DataCollectInDevice::KernelReplay(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc) {
    if (ProfConfig::Instance().IsDbi()) {
        return true;
    }
    replayCount_ = ProfConfig::Instance().GetInitReplayCount();
    bool funcRet{true};
    bool isMC2 = KernelContext::Instance().GetMC2Flag();
    L2cacheParam param{};
    param.blockLen = nullptr;
    param.l2Buffer = nullptr;
    param.stream = stream;
    isClearParamSuccess_ = !ProfConfig::Instance().IsAppReplay() && PrepareClearL2CacheParam(param);
    WarmUp(stream, kernelLaunchFunc);
    ProfCommandAction(MsprofCommandHandleType::PROF_COMMANDHANDLE_TYPE_START);
    if (SupportProfL2CacheEvict()) {
        bool res = ReplayOnce(stream, kernelLaunchFunc, param);
        profL2cacheEvict_ = false;
        if (ProfConfig::Instance().IsAppReplay()) {
            return res;
        }
    }
    for (; replayCount_ < GetReplayTimes() && !IsReceiveSignal(); replayCount_++) {
        auto profRes = ReplayOnce(stream, kernelLaunchFunc, param);
        funcRet = funcRet && profRes;
        if (ProfConfig::Instance().IsAppReplay()) {
            return funcRet;
        }
    }
    // MC2调优去掉Restore, 配合取消AICORE 劫持函数CALL的那次调用，取得最后一次重放的aicore的timeline
    if (!isMC2 && !KernelContext::Instance().GetLcclFlag()) {
        MemoryContext::Instance().Restore();
    }
    FreeL2Cache(param);
    return funcRet;
}

bool DataCollectInDevice::InstrProfData(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc) {
    if (ProfConfig::Instance().IsRangeReplay()) {
        return false;
    }
    DEBUG_LOG("Kernel running for instr prof, kernel name is %s", kernelName_.c_str());
    if (outputPath_.empty()) {
        return true;
    }
    if (!IsExist(outputPath_)) {
        return false;
    }
    bool isMC2 = KernelContext::Instance().GetMC2Flag();
    if (!ProfConfig::Instance().IsAppReplay() && !KernelContext::Instance().GetLcclFlag() && !isMC2 &&
        !MemoryContext::Instance().Backup()) {
        INFO_LOG("memoryContext failed!");
        return false;
    }
    DEBUG_LOG("Start instr profiling on device %d, kernel: %s", deviceId_, kernelName_.c_str());
    aclrtSynchronizeStreamImplOrigin(stream);
    auto ret = KernelLaunchForInstrProf(stream, kernelLaunchFunc);
    return ret;
}

bool DataCollectInDevice::SupportProfL2CacheEvict() {
    std::string socVersion = DeviceContext::Local().GetSocVersion();
    auto chipType = GetProductTypeBySocVersion(socVersion);
    if (!IsChipSeriesTypeValid(chipType, ChipProductType::ASCEND910B_SERIES) &&
        !IsChipSeriesTypeValid(chipType, ChipProductType::ASCEND910_93_SERIES)) {
        return false;
    }
    auto profConfig = ProfConfig::Instance().GetConfig();
    if (profConfig.aicPmu[0] != 0 && profConfig.aivPmu[0] != 0 && profConfig.l2CachePmu[0] != 0) {
        profL2cacheEvict_ = true;
        return true;
    }
    return false;
}

bool DataCollectInDevice::RangeReplayInit(bool &needReplay) {
    auto threadId = std::this_thread::get_id();
    RangeReplayConfig rangeConfig = ProfDataCollect::GetThreadRangeConfigMap(threadId);
    bool hasValidOutput =
        any_of(rangeConfig.outputs.begin(), rangeConfig.outputs.end(), [](const string &out) { return out != "-1"; });
    // if not in mstx range or no operator to optimize
    if (!rangeConfig.flag || !hasValidOutput) {
        needReplay = false;
        DEBUG_LOG("No need to range replay, device: %d.", deviceId_);
        ProfDataCollect::ResetRangeConfig(threadId);
        return true;
    }
    ProfDataCollect::ResetRangeConfig(threadId);
    string replayPath = JoinPath({GetEnv(DEVICE_PROF_DUMP_PATH_ENV), "device" + to_string(deviceId_),
        to_string(getpid()), to_string(rangeConfig.count)});
    if (!MkdirRecusively(replayPath)) {
        ERROR_LOG("Mkdir device %d range replay temp path failed.", deviceId_);
        return false;
    }
    // save replay operators output paths, one line for an operator, output path will be "-1" if no need to optimize
    string outputTxt = JoinPath({replayPath, "output.txt"});
    if (!CheckWriteFilePathValid(outputTxt)) {
        ERROR_LOG("Check file %s failed, maybe is not writeable.", outputTxt.c_str());
        return false;
    }
    std::ofstream outFile(outputTxt.c_str(), std::ios::out | std::ios::binary);
    if (!outFile.is_open()) {
        ERROR_LOG("Cannot open file [%s]", outputTxt.c_str());
        return false;
    }
    for (const auto &i : rangeConfig.outputs) {
        outFile << i << "\n";
    }
    outFile.close();
    Chmod(outputTxt, SAVE_DATA_FILE_AUTHORITY);
    {
        std::lock_guard<std::mutex> lk(rangeConfigMutex_);
        threadRangeConfigMap_[threadId].count += 1;
    }
    {
        std::lock_guard<std::mutex> lk(outputMutex_);
        deviceOutputPathMap_[deviceId_] = replayPath;
        DEBUG_LOG("set device %d range replay temp path: %s", deviceId_, replayPath.c_str());
    }
    return true;
}

bool DataCollectInDevice::RangeReplayOnce(L2cacheParam &param, const aclmdlRI &modelRI) {
    if (isClearParamSuccess_ && !ClearL2Cache(param)) {
        WARN_LOG("Clear L2Cache failed. replay count is %d", replayCount_ + 1);
    }
    if (!MemoryContext::Instance().Restore()) {
        WARN_LOG("Replay data restore failed. Skipping %d", replayCount_ + 1);
        return false;
    }
    if (!StartProf(readThread_)) {
        StopProf();
        WARN_LOG("Start profiling failed. Skipping %d", replayCount_ + 1);
        return false;
    }
    aclError ret = aclmdlRIExecuteAsyncImplOrigin(modelRI, param.stream);
    if (IsPmuEventEmpty(replayCount_ + 1)) {
        LoadFrequency();
        SaveFrequency(ProfDataCollect::GetAicoreOutputPath(deviceId_));
    }
    if (ret == ACL_ERROR_NONE) {
        ret = aclrtSynchronizeStreamWithTimeoutImplOrigin(param.stream, SYNCHRONIZE_TIME_OUT);
    }
    usleep(WAIT_DATA_READ_TIME); // 延时100us，防止duration数据读取不全
    StopProf();
    if (readThread_.joinable()) {
        readThread_.join();
    }
    if (ret == ACL_ERROR_NONE) {
        return true;
    } else {
        WARN_LOG("Kernel run on device %d No. %d time failed, res is %d.", deviceId_, replayCount_ + 1, ret);
        return false;
    }
}

bool DataCollectInDevice::RangeReplay(const rtStream_t &stream, const aclmdlRI &modelRI) {
    bool funcRet{false};
    bool needReplay{true};
    if (!RangeReplayInit(needReplay)) {
        return funcRet;
    }
    if (!needReplay) {
        return true;
    }
    MemoryContext::Instance().Backup();
    WarmUp(stream, modelRI);
    L2cacheParam param{};
    param.blockLen = nullptr;
    param.l2Buffer = nullptr;
    param.stream = stream;
    isClearParamSuccess_ = PrepareClearL2CacheParam(param);
    replayCount_ = ProfConfig::Instance().GetInitReplayCount();
    ProfCommandAction(MsprofCommandHandleType::PROF_COMMANDHANDLE_TYPE_START);
    for (; replayCount_ < GetReplayTimes() && !IsReceiveSignal(); replayCount_++) {
        if (replayCount_ != 0 && IsPmuEventEmpty(replayCount_)) {
            break;
        }
        DEBUG_LOG("Replaying round on device %d No. %d time", deviceId_, replayCount_ + 1);
        if (RangeReplayOnce(param, modelRI)) {
            funcRet = true;
        }
    }
    FreeL2Cache(param);
    return funcRet;
}

bool DataCollectInDevice::RangeReplayProfData(const rtStream_t &stream) {
    auto threadId = std::this_thread::get_id();
    RangeReplayConfig rangeConfig = ProfDataCollect::GetThreadRangeConfigMap(threadId);
    if (!rangeConfig.flag) {
        // for first operator in every mstx range, call aclmdlRICaptureBeginImpl
        auto res = aclmdlRICaptureBeginImplOrigin(stream, ACL_MODEL_RI_CAPTURE_MODE_GLOBAL);
        if (res != ACL_ERROR_NONE) {
            ERROR_LOG("Range replay begin failed, res is %d", res);
            return false;
        }
        {
            std::lock_guard<std::mutex> lk(rangeConfigMutex_);
            threadRangeConfigMap_[threadId].flag = true;
            threadRangeConfigMap_[threadId].stream = stream;
        }
    }
    string tmpOutput = outputPath_;
    if (outputPath_.empty()) {
        tmpOutput = "-1";
    }
    {
        std::lock_guard<std::mutex> lk(rangeConfigMutex_);
        threadRangeConfigMap_[threadId].outputs.emplace_back(tmpOutput);
    }
    return true;
}

bool DataCollectInDevice::ProfData(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc) {
    DEBUG_LOG("Kernel running, kernel name is %s", kernelName_.c_str());
    bool isRange = ProfConfig::Instance().IsRangeReplay() && MsTx::Instance().IsInMstxRange();
    if (isRange && !RangeReplayProfData(stream)) {
        return false;
    }
    if (outputPath_.empty()) {
        return true;
    }
    if (!IsExist(outputPath_)) {
        return false;
    }
    if (isRange) {
        INFO_LOG("Kernel will profiling on device %d, kernel: %s", deviceId_, kernelName_.c_str());
        SaveBasicInfo();
        return true;
    }
    if (!ProfConfig::Instance().IsAppReplay() && !KernelContext::Instance().GetLcclFlag() &&
        !MemoryContext::Instance().Backup()) {
        INFO_LOG("memoryContext failed!");
        return false;
    }
    INFO_LOG("Start profiling on device %d, kernel: %s", deviceId_, kernelName_.c_str());
    aclrtSynchronizeStreamImplOrigin(stream);
    bool isMC2 = KernelContext::Instance().GetMC2Flag();
    if (isMC2) {
        // kernel warmup, profiling do not take into account
        AicpuLaunchArgs &aicpuLaunchArgs = KernelContext::GetAicpuLaunchArgs();
        kernelLaunchFunc();
        aclrtSynchronizeStreamWithTimeoutImplOrigin(stream, SYNCHRONIZE_TIME_OUT);
        aclrtSynchronizeStreamWithTimeoutImplOrigin(aicpuLaunchArgs.stm, SYNCHRONIZE_TIME_OUT);
    }
    auto ret = KernelReplay(stream, kernelLaunchFunc);
    if (ret) {
        if (!ProfConfig::Instance().IsAppReplay() || !ProfConfig::Instance().IsDbi()) {
            SaveBasicInfo();
        }
    }
    if (ProfConfig::Instance().GetConfig().isDeviceToSimulator && !isMC2) {
        if (launchCtx_ != nullptr) {
            auto launchId = launchCtx_->GetLaunchParam().launchId;
            simulatorLauncher.Launch(outputPath_, launchId, true);
        } else {
            simulatorLauncher.Launch(outputPath_);
        }
    }
    return ret;
}

void DataCollectInDevice::WarmUp(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc) const {
    uint16_t warmUpTimes = ProfConfig::Instance().GetWarmUpTimes();
    WarmUp(stream, kernelLaunchFunc, warmUpTimes);
}

void DataCollectInDevice::WarmUp(
    rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc, uint16_t warmUpTimes) const {
    if (warmUpTimes == 0 || KernelContext::Instance().GetMC2Flag() || KernelContext::Instance().GetLcclFlag()) {
        return;
    }
    INFO_LOG("Warm Up enabled. times:%d", warmUpTimes);
    if (!ProfConfig::Instance().IsAppReplay()) {
        for (uint16_t i = 0; i < warmUpTimes; ++i) {
            kernelLaunchFunc();
        }
        if (aclrtSynchronizeStreamImplOrigin(stream) != ACL_SUCCESS) {
            return;
        }
        DEBUG_LOG("Warm Up success in kernel replay mode.");
        return;
    }
    uint8_t *memInfoA = nullptr;
    uint8_t *memInfoB = nullptr;
    if (aclrtMallocImplOrigin(reinterpret_cast<void **>(&memInfoA), MB_TO_BYTES, ACL_MEM_MALLOC_HUGE_FIRST) !=
        ACL_SUCCESS) {
        return;
    }
    std::shared_ptr<void> deferA(nullptr, [&memInfoA](std::nullptr_t &) { aclrtFreeImplOrigin(memInfoA); });
    if (aclrtMallocImplOrigin(reinterpret_cast<void **>(&memInfoB), MB_TO_BYTES, ACL_MEM_MALLOC_HUGE_FIRST) !=
        ACL_SUCCESS) {
        return;
    }
    std::shared_ptr<void> deferB(nullptr, [&memInfoB](std::nullptr_t &) { aclrtFreeImplOrigin(memInfoB); });
    for (uint16_t i = 0; i < warmUpTimes; ++i) {
        if (aclrtMemcpyAsyncImplOrigin(
                memInfoB, MB_TO_BYTES, memInfoA, MB_TO_BYTES, ACL_MEMCPY_DEVICE_TO_DEVICE, stream) != ACL_SUCCESS) {
            return;
        }
    }
    if (aclrtSynchronizeStreamImplOrigin(stream) != RT_ERROR_NONE) {
        return;
    }
    DEBUG_LOG("Warm Up success in application replay mode.");
}

void DataCollectInDevice::WarmUp(const rtStream_t &stream, const aclmdlRI &modelRI) const {
    uint16_t warmUpTimes = ProfConfig::Instance().GetWarmUpTimes();
    if (warmUpTimes == 0) {
        return;
    }
    INFO_LOG("Warm Up enabled. times:%d", warmUpTimes);
    for (uint16_t i = 0; i < warmUpTimes; ++i) {
        aclmdlRIExecuteAsyncImplOrigin(modelRI, stream);
    }
    if (aclrtSynchronizeStreamWithTimeoutImplOrigin(stream, SYNCHRONIZE_TIME_OUT) == ACL_ERROR_NONE) {
        DEBUG_LOG("Warm Up success in range replay mode.");
    }
}

bool DataCollectInDevice::PrepareClearL2CacheParam(L2cacheParam &param) const {
    std::string socVersion = DeviceContext::Local().GetSocVersion();
    auto it = l2CacheClearTilingMap.find(socVersion);
    if (it == l2CacheClearTilingMap.end()) {
        WARN_LOG("Cannot get L2Cache info when clear L2Cache.");
        return false;
    }
    uint64_t bufferSize = it->second.clearSizePerCore * it->second.aicCoreNum;
    param.bufferSize = bufferSize;
    if (!MallocBuffer(param)) {
        return false;
    }
    if (aclrtMallocImplOrigin(&param.l2Buffer, bufferSize, ACL_MEM_MALLOC_HUGE_FIRST) != ACL_SUCCESS) {
        WARN_LOG("Malloc buffer failed when clear L2Cache.");
        return false;
    }
    void *hostBlockLen;
    if (aclrtMallocHostImplOrigin(&hostBlockLen, sizeof(uint64_t)) != ACL_SUCCESS) {
        WARN_LOG("Malloc host tiling info failed when clear L2Cache.");
        aclrtFreeImplOrigin(param.l2Buffer);
        return false;
    }
    shared_ptr<void> hostBlockLenDefer(
        nullptr, [&hostBlockLen](std::nullptr_t &) { aclrtFreeHostImplOrigin(hostBlockLen); });
    *(uint64_t *)hostBlockLen = it->second.clearSizePerCore;
    // runtime接口好像不会补齐到32字节，申请小块的内存会出现访问时直接挂掉的情况
    if (aclrtMallocImplOrigin(&param.blockLen, 32, ACL_MEM_MALLOC_HUGE_FIRST) != ACL_SUCCESS) {
        WARN_LOG("Malloc tiling info failed when clear L2Cache.");
        aclrtFreeImplOrigin(param.l2Buffer);
        return false;
    }
    if (aclrtMemcpyAsyncImplOrigin(param.blockLen, sizeof(uint64_t), hostBlockLen, sizeof(uint64_t),
            ACL_MEMCPY_HOST_TO_DEVICE, param.stream) != ACL_SUCCESS ||
        aclrtSynchronizeStreamImplOrigin(param.stream) != ACL_SUCCESS) {
        WARN_LOG("Memcpy tiling info failed when clear L2Cache.");
        aclrtFreeImplOrigin(param.l2Buffer);
        aclrtFreeImplOrigin(param.blockLen);
        return false;
    }
    param.blockDim = it->second.aicCoreNum;
    DEBUG_LOG("Prepare clear L2Cache param success.");
    return true;
}

// 初始化3个buffer，从flushbuffer搬运到buffer上，清除L2cache数据；cmobuffer用于将buffersize的数据预取到L2cahce，完成L2cache的清除
bool DataCollectInDevice::MallocBuffer(L2cacheParam &param) const {
    auto ret = CheckAclResult(
        aclrtMallocImplOrigin(&param.buffer, param.bufferSize, ACL_MEM_MALLOC_HUGE_FIRST), "buffer aclrtMallocImpl");
    if (ret != ACL_SUCCESS) {
        return false;
    }
    ret = CheckAclResult(aclrtMallocImplOrigin(&param.flushBuffer, param.bufferSize, ACL_MEM_MALLOC_HUGE_FIRST),
        "flush buffer aclrtMallocImpl");
    if (ret != ACL_SUCCESS) {
        return false;
    }
    ret = CheckAclResult(aclrtMallocImplOrigin(&param.cmoBuffer, param.bufferSize, ACL_MEM_MALLOC_HUGE_FIRST),
        "cmoBuffer aclrtMallocImpl");
    return ret == ACL_SUCCESS;
}

bool DataCollectInDevice::WaitClearL2Cache(L2cacheParam &param) const {
    const std::string &runSocVersion = DeviceContext::Local().GetSocVersion();
    auto ret = CheckAclResult(aclrtMemcpyAsyncImplOrigin(param.flushBuffer, param.bufferSize, param.buffer,
                                  param.bufferSize, ACL_MEMCPY_DEVICE_TO_DEVICE, param.stream),
        "aclrtMemcpyAsyncImpl");
    if (ret != ACL_SUCCESS) {
        return false;
    }
    ret = CheckAclResult(aclrtSynchronizeStreamImplOrigin(param.stream), "aclrtSynchronizeStreamImpl");
    if (ret != ACL_SUCCESS) {
        return false;
    }
    auto chipType = GetProductTypeBySocVersion(runSocVersion);
    if (IsChipSeriesTypeValid(chipType, ChipProductType::ASCEND910B_SERIES) ||
        IsChipSeriesTypeValid(chipType, ChipProductType::ASCEND910_93_SERIES)) {
        ret = CheckAclResult(
            aclrtCmoAsyncImplOrigin(param.cmoBuffer, param.bufferSize, ACL_RT_CMO_TYPE_PREFETCH, param.stream),
            "aclrtCmoAsyncImpl");
        if (ret != ACL_SUCCESS) {
            return false;
        }
    }
    ret = CheckAclResult(aclrtSynchronizeStreamImplOrigin(param.stream), "aclrtSynchronizeStreamImpl");
    return ret == ACL_SUCCESS;
}

void DataCollectInDevice::FreeL2Cache(L2cacheParam &param) const {
    if (param.flushBuffer != nullptr) {
        CheckAclResult(aclrtFreeImplOrigin(param.flushBuffer), "flush buffer aclrtFreeImpl");
    }
    if (param.cmoBuffer != nullptr) {
        CheckAclResult(aclrtFreeImplOrigin(param.cmoBuffer), "cmo buffer aclrtFreeImpl");
    }
    if (param.buffer != nullptr) {
        CheckAclResult(aclrtFreeImplOrigin(param.buffer), "buffer aclrtFreeImpl");
    }
    if (param.l2Buffer != nullptr) {
        CheckAclResult(aclrtFreeImplOrigin(param.l2Buffer), "l2Buffer aclrtFreeImpl");
    }
    if (param.blockLen != nullptr) {
        CheckAclResult(aclrtFreeImplOrigin(param.blockLen), "blockLen aclrtFreeImpl");
    }
}

bool DataCollectInDevice::ClearL2Cache(L2cacheParam &param) const {
    std::vector<void *> inputArgs = {param.l2Buffer, param.blockLen};
    if (!DFXKernelLauncher::Instance().CallClearL2Cache(param.blockDim, param.stream, inputArgs)) {
        WARN_LOG("Failed to clear L2cache by operator");
        return false;
    }
    if (!WaitClearL2Cache(param)) {
        WARN_LOG("Failed to clear L2cache by move memory to L2");
        return false;
    }
    return true;
}

bool DataCollectInDevice::CallEmptyKernel(void *stream) const {
    if (!DFXKernelLauncher::Instance().CallEmptyKernel(stream)) {
        WARN_LOG("Failed to call empty kernel for L2cache");
        return false;
    }
    return true;
}

void DataCollectInDevice::LoadFrequency() {
    // get rated freq
    auto ret = halGetDeviceInfoOrigin(deviceId_, MODULE_TYPE_AICORE, INFO_TYPE_FREQUE, &ratedFreq_);
    if (ret != tagDrvError::DRV_ERROR_NONE) {
        WARN_LOG("Can not get rated aicore frequency.Please check dlopen function.");
        ratedFreq_ = -1;
    }
    // get current freq
    int32_t size = sizeof(int32_t);
    ret = halGetDeviceInfoByBuffOrigin(
        deviceId_, MODULE_TYPE_AICORE, INFO_TYPE_CURRENT_FREQ, static_cast<void *>(&curFreq_), &size);
    if (ret != tagDrvError::DRV_ERROR_NONE) {
        WARN_LOG("Can not get current aicore frequency.Please check dlopen function. Error code:%d", ret);
        curFreq_ = -1;
    }
    DEBUG_LOG("get current freq: %d, rated freq: %ld.", curFreq_, ratedFreq_);
}

void DataCollectInDevice::SaveBasicInfo() {
    std::string basicInfoTxt = JoinPath({outputPath_, "op_basic_info.txt"});
    if (!CheckWriteFilePathValid(basicInfoTxt)) {
        ERROR_LOG("check file: %s failed", basicInfoTxt.c_str());
        return;
    }
    std::ofstream outFile(basicInfoTxt.c_str(), std::ios::out | std::ios::binary);

    if (!outFile.is_open()) {
        ERROR_LOG("Cannot create file [%s]", basicInfoTxt.c_str());
        return;
    }
    const std::string runSocVersion = DeviceContext::Local().GetSocVersion();
    uint32_t blockDim;
    if (launchCtx_ != nullptr) {
        blockDim = launchCtx_->GetLaunchParam().blockDim;
    } else {
        blockDim = KernelContext::Instance().GetBlockId();
    }
    std::string newKernelName = KernelNameConver(kernelName_);
    outFile << "Op Name=" << newKernelName << "\n";
    outFile << "Block Dim=" << blockDim << "\n";
    outFile << "Run Soc Version=" << runSocVersion << "\n";
    outFile << "Device Id=" << deviceId_ << "\n";
    if (!ProfConfig::Instance().IsAppReplay()) {
        outFile << "Pid=" << getpid() << "\n";
    }
    if (!ProfConfig::Instance().IsRangeReplay()) {
        if (curFreq_ != -1) {
            outFile << "Current Freq=" << curFreq_ << "\n";
        } else {
            outFile << "Current Freq=" << "NA" << "\n";
        }
        if (ratedFreq_ != -1) {
            outFile << "Rated Freq=" << ratedFreq_ << "\n";
        } else {
            outFile << "Rated Freq=" << "NA" << "\n";
        }
    }
    outFile << "Is MC2=" << static_cast<int>(KernelContext::Instance().GetMC2Flag()) << "\n";
    outFile << "Is Lccl=" << static_cast<int>(KernelContext::Instance().GetLcclFlag()) << "\n";
    outFile << "Has Simt=" << static_cast<int>(hasSimt_) << "\n";
    outFile.close();
    Chmod(basicInfoTxt, SAVE_DATA_FILE_AUTHORITY);
}

void DataCollectInDevice::SaveFrequency(const string &outputPath) const {
    std::string outputFile = JoinPath({outputPath, "freq.txt"});
    if (!CheckWriteFilePathValid(outputFile)) {
        ERROR_LOG("check file: %s failed", outputFile.c_str());
        return;
    }
    std::ofstream outFile(outputFile.c_str(), std::ios::out | std::ios::binary);
    if (!outFile.is_open()) {
        ERROR_LOG("Cannot create file [%s]", outputFile.c_str());
        return;
    }
    if (curFreq_ != -1) {
        outFile << "Current Freq=" << curFreq_ << "\n";
    } else {
        outFile << "Current Freq=" << "NA" << "\n";
    }
    if (ratedFreq_ != -1) {
        outFile << "Rated Freq=" << ratedFreq_ << "\n";
    } else {
        outFile << "Rated Freq=" << "NA" << "\n";
    }
    outFile.close();
    Chmod(outputFile, SAVE_DATA_FILE_AUTHORITY);
}

ProfDataCollect::ProfDataCollect(const LaunchContextSP &ctx, bool isInitOutput) {
    if (ProfConfig::Instance().IsSimulator()) {
        dataCollect_ = MakeShared<DataCollectWithSimulator>(ctx);
    } else {
        dataCollect_ = MakeShared<DataCollectInDevice>(ctx, isInitOutput);
    }
}

bool ProfDataCollect::InstrProfData(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc) {
    return dataCollect_->InstrProfData(stream, kernelLaunchFunc);
}

bool ProfDataCollect::ProfData(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc) {
    return dataCollect_->ProfData(stream, kernelLaunchFunc);
}

bool ProfDataCollect::ProfData() const { return dataCollect_->ProfData(); }

void ProfDataCollect::ProfInit(const void *hdl, const void *stubFunc, bool type) const {
    dataCollect_->ProfInit(hdl, stubFunc, type);
}

bool ProfDataCollect::SaveObject(const void *hdl) { return DataCollectWithSimulator::SaveObject(hdl); }

bool ProfDataCollect::SaveObject(const RegisterContextSP &ctx) { return DataCollectWithSimulator::SaveObject(ctx); }

void ProfDataCollect::TerminateInAdvance() const {
    auto currentDeviceId = DeviceContext::GetRunningDeviceId();
    INFO_LOG("Kill op process on device %d for the number of kernel reaches collection range.", currentDeviceId);
    (void)LocalProcess::GetInstance().TerminateWithSignal(SIGINT);
    constexpr uint32_t killWaitTime = 30U;
    std::this_thread::sleep_for(std::chrono::seconds(killWaitTime));
    (void)LocalProcess::GetInstance().TerminateWithSignal(SIGKILL);
}

void ProfDataCollect::PostProcess() const {
    // 与服务端进行采集后通信
    ProcessCtrl::Rsp rsp{};
    if (ProfConfig::Instance().PostNotify(rsp) && rsp.termination != 0) {
        TerminateInAdvance();
    }
}

void ProfDataCollect::GenBBcountFile(uint64_t regId, uint64_t memSize, uint8_t *memInfo) const {
    dataCollect_->GenBBcountFile(regId, memSize, memInfo);
}
bool ProfDataCollect::IsNeedProf() const { return (!dataCollect_->outputPath_.empty()); }

bool ProfDataCollect::IsBBCountNeedGen() {
    auto config = ProfConfig::Instance().GetConfig();
    return ((config.dbiFlag & DBI_FLAG_BB_COUNT) && IsNeedProf() && !KernelContext::Instance().GetMC2Flag() &&
        !KernelContext::Instance().GetLcclFlag());
}

bool ProfDataCollect::IsNeedDumpContext() {
    return IsNeedProf() && !KernelContext::Instance().GetMC2Flag() &&
        ProfConfig::Instance().GetConfig().isDeviceToSimulator;
}

bool ProfDataCollect::IsMemoryChartNeedGen() {
    auto config = ProfConfig::Instance().GetConfig();
    return ((config.dbiFlag & DBI_FLAG_MEMORY_CHART) && IsNeedProf() && !KernelContext::Instance().GetMC2Flag() &&
        !KernelContext::Instance().GetLcclFlag());
}

bool ProfDataCollect::IsOperandRecordNeedGen() {
    auto config = ProfConfig::Instance().GetConfig();
    return ((config.dbiFlag & DBI_FLAG_OPERAND_RECORD) && IsNeedProf() && !KernelContext::Instance().GetMC2Flag() &&
        !KernelContext::Instance().GetLcclFlag() && !ProfConfig::Instance().IsRangeReplay());
}

bool ProfDataCollect::IsPCSamplingNeedGen() { return (ProfConfig::Instance().IsPCSamplingEnabled() && IsNeedProf()); }

bool ProfDataCollect::IsTimelineNeedGen(ProfDBIType &type) {
    if (!IsNeedProf()) {
        return false;
    }
    return ProfConfig::Instance().IsInstrTimelineEnabled() ? (type = ProfDBIType::INSTR_PROF_DFX, true)
        : ProfConfig::Instance().IsPipeTimelineEnabled()   ? (type = ProfDBIType::INSTR_PROF_END, true)
                                                           : false;
}

bool ProfDataCollect::IsWarpTimelineNeedGen() {
    return (ProfConfig::Instance().IsWarpTimelineEnabled() && IsNeedProf() && dataCollect_->hasSimt_ &&
        !ProfConfig::Instance().IsRangeReplay());
}

bool ProfDataCollect::IsNeedRunOriginLaunch() {
    const auto &config = ProfConfig::Instance().GetConfig();
    // application模式下只有bbcount桩才需要调用origin，其他模式都不需要，因为bbcount桩需要依赖Call这次运行。
    // 所有通算融合算子都不需要调用origin
    if (KernelContext::Instance().GetMC2Flag() || KernelContext::Instance().GetLcclFlag()) {
        return false;
    }
    if (ProfConfig::Instance().IsAppReplay() && config.dbiFlag == DBI_FLAG_WARP_TIMELINE && !dataCollect_->hasSimt_) {
        DEBUG_LOG("Skip warp timeline collection because kernel has no simt symbol.");
        return true;
    }
    return !IsNeedProf() || !(config.dbiFlag != DBI_FLAG_BB_COUNT && ProfConfig::Instance().IsAppReplay());
}

void ProfDataCollect::GenDBIData(uint64_t memSize, uint8_t *memInfo) const {
    dataCollect_->GenDBIData(memSize, memInfo);
}

void ProfDataCollect::GenRecordData(uint64_t memSize, uint8_t *memInfo, const std::string &recordName) const {
    dataCollect_->GenRecordData(memSize, memInfo, recordName);
}

bool ProfDataCollect::RangeReplay(const rtStream_t &stream, const aclmdlRI &modelRI) {
    return dataCollect_->RangeReplay(stream, modelRI);
}

std::string ProfDataCollect::GetAicoreOutputPath(int32_t device) {
    std::lock_guard<std::mutex> lk(DataCollect::outputMutex_);
    if (DataCollect::deviceOutputPathMap_.find(device) == DataCollect::deviceOutputPathMap_.end()) {
        ERROR_LOG("Can not find device %d output path", device);
        return "";
    }
    return DataCollect::deviceOutputPathMap_[device];
}

uint32_t ProfDataCollect::GetDeviceReplayCount(int32_t device) {
    std::lock_guard<std::mutex> lk(DataCollect::replayCountMutex_);
    if (DataCollect::deviceReplayCountMap_.find(device) == DataCollect::deviceReplayCountMap_.end()) {
        DEBUG_LOG("Can not find device %d replay count, use default 0.", device);
        DataCollect::deviceReplayCountMap_[device] = 0;
    }
    return DataCollect::deviceReplayCountMap_[device];
}

RangeReplayConfig ProfDataCollect::GetThreadRangeConfigMap(std::thread::id threadId) {
    std::lock_guard<std::mutex> lk(DataCollect::rangeConfigMutex_);
    if (DataCollect::threadRangeConfigMap_.find(threadId) == DataCollect::threadRangeConfigMap_.end()) {
        DEBUG_LOG("Can not find threadId %zu range replay config, use default value.",
            std::hash<std::thread::id>()(threadId));
        DataCollect::threadRangeConfigMap_[threadId] = {false, 0, nullptr, {}};
    }
    return DataCollect::threadRangeConfigMap_[threadId];
}

void ProfDataCollect::ResetRangeConfig(std::thread::id threadId) {
    // reset range replay flag and outputs
    std::lock_guard<std::mutex> lk(DataCollect::rangeConfigMutex_);
    if (DataCollect::threadRangeConfigMap_.find(threadId) != DataCollect::threadRangeConfigMap_.end()) {
        DataCollect::threadRangeConfigMap_[threadId].flag = false;
        DataCollect::threadRangeConfigMap_[threadId].stream = nullptr;
        DataCollect::threadRangeConfigMap_[threadId].outputs = {};
    }
}
