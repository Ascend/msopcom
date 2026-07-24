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


#ifndef __PROF_TYPE_H__
#define __PROF_TYPE_H__
#include <cstdint>
#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "core/BinaryInstrumentation.h"

#include "include/opprof/Protocol.h"
#include "include/opprof/DbiDefs.h"

constexpr char const *PCOFFSET_RECORD = "PcOffset.bin";
constexpr char const *START_STUB_COMPILER_ARGS = "--kernel-bounded-pcstb-mode";
constexpr char const *DEVICE_TO_SIMULATOR = {"DEVICE_TO_SIMULATOR"};
constexpr char const *IS_SIMULATOR_ENV = {"IS_SIMULATOR_ENV"};
constexpr char const *DEVICE_PROF_DUMP_PATH_ENV = {"DEVICE_PROF_DUMP_PATH"};

uint64_t GetCoreNumForDbi(uint64_t blockDim);

class ProfConfig {
public:
    static ProfConfig &Instance()
    {
        static ProfConfig inst;
        return inst;
    }
    void Reset();

    void Init(const MessageOfProfConfig &profConfig) { profConfig_ = profConfig; }

    const MessageOfProfConfig &GetConfig() const { return profConfig_; }

    uint32_t GetInitReplayCount() const { return isAppReplay_ ? profConfig_.replayCount : 0U; }

    uint16_t GetWarmUpTimes() const
    {
        constexpr uint16_t WARMUP_MAX_TIMES = 500U;
        return profConfig_.profWarmUpTimes <= WARMUP_MAX_TIMES ? profConfig_.profWarmUpTimes : WARMUP_MAX_TIMES;
    }

    bool IsAppReplay() const { return isAppReplay_; }

    bool IsRangeReplay() const { return isRangeReplay_; }

    bool IsSimulator() const { return profConfig_.isSimulator; }

    bool IsEnablePmSampling() const { return profConfig_.pmSamplingEnable; }

    bool IsInstrTimelineEnabled() const { return profConfig_.dbiFlag & DBI_FLAG_INSTR_PROF_DFX; }

    bool IsPipeTimelineEnabled() const { return profConfig_.dbiFlag & DBI_FLAG_INSTR_PROF_END; }

    bool IsPCSamplingEnabled() const { return profConfig_.dbiFlag & DBI_FLAG_INSTR_PROF_START; }

    bool IsWarpTimelineEnabled() const { return profConfig_.dbiFlag & DBI_FLAG_WARP_TIMELINE; }

    bool IsDbi() const
    {
        return isAppReplay_ && (profConfig_.dbiFlag != 0);
    }

    void RequestLogTranslate(const std::string &outputPath, const std::string &kernelName);

    bool IsEnableLogTrans() const;

    void SetLogTransFlag(bool transFlag);

    void RestoreMemoryByMode() const;

    void NotifyStopTransLog();

    const std::string &GetSocVersion() const
    {
        return socVersion_;
    }

    std::string GetInstrTimelinePipe() const;

    std::string GetOutputPathFromRemote(const std::string &kernelName, int32_t deviceId);

    bool PostNotify(ProcessCtrl::Rsp &rsp); // 采集后通信

    bool IsSimulatorLaunchedByDevice() const;

    std::string GetPluginPath(ProfDBIType pluginType = ProfDBIType::MEMORY_CHART) const;

    void SendMsg(const std::string &msg);

    std::string GetMsopprofPath() const;


private:
    ProfConfig();
    ~ProfConfig() = default;

    void InitProfConfig();
    void InitConfigData();
    void LoadSocVersionFromEnv();
    std::mutex communtionMx_;
    MessageOfProfConfig profConfig_ {};
    std::string socVersion_;
    bool isAppReplay_ {false};
    bool isRangeReplay_ {false};
    bool isCaLogTrans_ {false};
};
#endif // __PROF_TYPE_H__
