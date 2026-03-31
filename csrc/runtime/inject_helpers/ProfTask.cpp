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

#include "ProfTask.h"
#include <atomic>
#include <thread>
#include "KernelContext.h"
#include "utils/Ustring.h"
#include "ascend_hal/AscendHalOrigin.h"
#include "utils/FileSystem.h"
#include "utils/InjectLogger.h"
#include "runtime/RuntimeOrigin.h"
#include "ProfDataCollect.h"
#include "DeviceContext.h"

using namespace std;

constexpr int PROF_CHANNEL_BUFFER_SIZE = 1024 * 1024 * 2;
constexpr int PROF_CHANNEL_NUM = 18;
constexpr uint32_t FFTS_PROF_CFG_MODE = 3;
constexpr uint32_t FFTS_PROF_CFG_TYPE = 0x5;
constexpr uint32_t FFTS_PROF_CFG_CORE_MASK = 0xffffffff;
constexpr uint32_t PROF_CFG_PERIOD = 0;
constexpr uint32_t AICPU_SAMPLE_PERIOD = 100;
constexpr uint32_t STARS_ENABLE_FLAG = 1;
constexpr uint32_t AICORE_TAG = 0;
constexpr uint32_t AICORE_TASK_BASE_TYPE = 0;
constexpr uint32_t HWTS_TAG = 0;
constexpr uint32_t MAX_BIN_FILE_SIZE = 1024 * 1024 * 1024;
constexpr int FFTS_PROF_MODE_BULT = 2;
constexpr int FFTS_PROF_BLOCK_SHINK_DISABLE = 2;
constexpr int FFTS_PROF_AIC_SCALE_ALL = 0;
constexpr int FFTS_PROF_AIC_SCALE_PARTIAL = 1;
constexpr int INSTR_PROF_PERIOD = 32;

// ts data code
using StarsSocLogConfigT = struct TagStarsSocLogConfig {
    uint32_t acsq_task;         // 1-enable,2-disable
    uint32_t acc_pmu;           // 1-enable,2-disable
    uint32_t cdqm_reg;          // 1-enable,2-disable
    uint32_t dvpp_vpc_block;    // 1-enable,2-disable
    uint32_t dvpp_jpegd_block;  // 1-enable,2-disable
    uint32_t dvpp_jpede_block;  // 1-enable,2-disable
    uint32_t ffts_thread_task;  // 1-enable,2-disable
    uint32_t ffts_block;        // 1-enable,2-disable
    uint32_t sdma_dmu;          // 1-enable,2-disable
};
using TsAiCoreProfileConfigT = struct TagTsAiCoreProfileConfig {
    uint32_t type;                      // 0-task base, 1-sample base
    uint32_t almostFullThreshold;     // sample base
    uint32_t period;                    // sample base
    uint32_t coreMask;                 // sample base
    uint32_t eventNum;                 // public
    uint32_t event[PMU_EVENT_MAX_NUM];  // public
    uint32_t tag;                       // bit0=0 enable immediately; bit0=1 enable delay
};

using StarsAiCoreProfileConfigT = struct TagStarsAiCoreConfig {
    uint32_t type;                     // bit0：task base | bit1：sample base | bit2：blk task | bit3：sub task
    uint32_t period;                   // sample base
    uint32_t core_mask;                // sample base
    uint32_t event_num;                // public
    uint16_t event[PMU_EVENT_MAX_NUM]; // public
};

using FftsProfileConfigT = struct TagFftsProfileConfig {
    uint32_t cfgMode;      // 0-none, 1-aic, 2-aiv, 3-aic&aiv
    StarsAiCoreProfileConfigT aiEventCfg[FFTS_PROF_MODE_BULT];
    uint32_t tag = 0;
};


using AccProfileConfigT = struct TagAccProfileConfig {
    uint32_t type;                         // bit0:task base | bit1:sample base | bit2:blk task | bit3:sub task
    uint32_t period;                       // sample base
    uint32_t coreMask;                     // sample base
    uint32_t eventNum;                     // public
    uint16_t event[10]; // public
};

using StarsA5SocLogConfigT = struct TagStarsA5SocLogConfig {
    uint32_t acsq_task;         // 1-enable,2-disable
    uint32_t acc_pmu;           // 1-enable,2-disable
    uint32_t cdqm_reg;          // 1-enable,2-disable
    uint32_t dvpp_vpc_block;    // 1-enable,2-disable
    uint32_t dvpp_jpegd_block;  // 1-enable,2-disable
    uint32_t dvpp_jpede_block;  // 1-enable,2-disable
    uint32_t ffts_thread_task;  // 1-enable,2-disable
    uint32_t ffts_block;        // 1-enable,2-disable
    uint32_t sdma_dmu;          // 1-enable,2-disable
    uint32_t tag;               // bit0==0-enable immediately, bit0==1-enable delay
    uint32_t block_shrink_flag;   // 1-enable,2-disable
};

using FftsA5ProfileConfigT = struct TagFftsA5ProfileConfig {
    uint32_t cfgMode;            // 0-none, 1-aic, 2-aiv, 3-aic&aiv
    AccProfileConfigT aiEventCfg[FFTS_PROF_MODE_BULT];
    uint32_t tag;                // 0-enable immediately, 1-enable delay
    uint32_t blockShinkFlag;     // 1-enable, 2-disable
    uint32_t aicScale;           // 0-all, 1-partial
};

using InstrProfileConfigT = struct TagInstrProfileConfig {
    uint32_t period;               // cycle .. BIU PERF不使用
    uint32_t biuPcSamplingMode;    // 0: biu instr mode, 1: pc sampling
    uint32_t groupType;            // 0: aic, 1: aiv0, 2: aiv1
    uint32_t groupNo;              // group 编号
};

using InstrProfHeadInfoT = struct TagInstrProfHeadInfo {
    uint16_t coreId = 0;
    uint16_t coreType = 0;
    uint32_t validLen = 0;
};

enum class InstrProfGrpType : uint32_t {
    AIC = 0,
    AIV0,
    AIV1,
};

enum class InstrProfGrpId : uint32_t {
    GROUP0 = 0,
    GROUP1,
    GROUP2,
    GROUP3,
    GROUP4,
    GROUP5,
};

struct InstrProfChnInfo {
    InstrProfGrpType grpType;
    InstrProfGrpId grpId;
};

class ProfTaskOfA5 : public ProfTask {
public:
    ProfTaskOfA5(const MessageOfProfConfig &profTaskConfig, uint32_t deviceId)
        : ProfTask(profTaskConfig, deviceId)
    {
        instrProfChn_ = {
            {InstrChannel::GROUP0_AIC, {InstrProfGrpType::AIC, InstrProfGrpId::GROUP0}},
            {InstrChannel::GROUP0_AIV0, {InstrProfGrpType::AIV0, InstrProfGrpId::GROUP0}},
            {InstrChannel::GROUP0_AIV1, {InstrProfGrpType::AIV1, InstrProfGrpId::GROUP0}},
            {InstrChannel::GROUP1_AIC, {InstrProfGrpType::AIC, InstrProfGrpId::GROUP1}},
            {InstrChannel::GROUP1_AIV0, {InstrProfGrpType::AIV0, InstrProfGrpId::GROUP1}},
            {InstrChannel::GROUP1_AIV1, {InstrProfGrpType::AIV1, InstrProfGrpId::GROUP1}},
            {InstrChannel::GROUP2_AIC, {InstrProfGrpType::AIC, InstrProfGrpId::GROUP2}},
            {InstrChannel::GROUP2_AIV0, {InstrProfGrpType::AIV0, InstrProfGrpId::GROUP2}},
            {InstrChannel::GROUP2_AIV1, {InstrProfGrpType::AIV1, InstrProfGrpId::GROUP2}},
            {InstrChannel::GROUP3_AIC, {InstrProfGrpType::AIC, InstrProfGrpId::GROUP3}},
            {InstrChannel::GROUP3_AIV0, {InstrProfGrpType::AIV0, InstrProfGrpId::GROUP3}},
            {InstrChannel::GROUP3_AIV1, {InstrProfGrpType::AIV1, InstrProfGrpId::GROUP3}},
            {InstrChannel::GROUP4_AIC, {InstrProfGrpType::AIC, InstrProfGrpId::GROUP4}},
            {InstrChannel::GROUP4_AIV0, {InstrProfGrpType::AIV0, InstrProfGrpId::GROUP4}},
            {InstrChannel::GROUP4_AIV1, {InstrProfGrpType::AIV1, InstrProfGrpId::GROUP4}},
            {InstrChannel::GROUP5_AIC, {InstrProfGrpType::AIC, InstrProfGrpId::GROUP5}},
            {InstrChannel::GROUP5_AIV0, {InstrProfGrpType::AIV0, InstrProfGrpId::GROUP5}},
            {InstrChannel::GROUP5_AIV1, {InstrProfGrpType::AIV1, InstrProfGrpId::GROUP5}},
        };
    }
    bool Start(uint32_t replayCount, bool hasSimt) override;
    void Stop() override;
private:
    bool WriteInstrChannelData(const std::string &prefixName, InstrChannel channelId,
        const char *data, int validLen, InstrChnReadCtrl &instrChnReadController) override;
    bool StartInstrProfTask(int mode);
    void StopInstrProfChannels();
    std::map<InstrChannel, InstrProfChnInfo> instrProfChn_;
    // InstrProf 采集状态机
    std::atomic<InstrProfState> instrProfState_ {InstrProfState::IDLE};
private:
    bool isStarsStart_ = false;
    bool StartStarsTask();
    bool StartFFTSTask(uint32_t replayCount);
};

class ProfTaskOf910B : public ProfTask {
public:
    ProfTaskOf910B(const MessageOfProfConfig &profTaskConfig, uint32_t deviceId)
        : ProfTask(profTaskConfig, deviceId) {}
    bool Start(uint32_t replayCount, bool hasSimt) override;
    void Stop() override;
private:
    bool isStarsStart_ = false;
    bool StartHCCSTask();
    bool StartTSFWTask();
    bool StartStarsTask();
    bool StartFFTSTask(uint32_t replayCount);
};

class ProfTaskOf310P : public ProfTask {
public:
    ProfTaskOf310P(const MessageOfProfConfig &profTaskConfig, uint32_t deviceId)
        : ProfTask(profTaskConfig, deviceId) {}
    bool Start(uint32_t replayCount, bool hasSimt) override;
    void Stop() override;
private:
    bool StartHwtsTask();
    bool isL2CacheStart_ = false;
    bool isAiCoreStart_ = false;
    bool isHwtsStart_ = false;
};

// PCSampling BiuPerf 共用通道
class InstrProfTask {
public:
    bool GetTask(int mode, prof_start_para_t &instrProfStartPara) const;
};

class FftsA5Task {
public:
    bool GetTask(uint16_t *aicEventPtr, uint16_t *aivEventPtr, prof_start_para_t &fftsA5ProfStartPara) const;
    bool useProfileMode {false};
};

class FftsTask {
public:
    bool GetTask(uint16_t *aicEventPtr, uint16_t *aivEventPtr, prof_start_para_t &fftsProfStartPara) const;
    bool useProfileMode {false};
};
class StarsTask {
public:
    bool GetTask(prof_start_para_t &starsProfStartPara) const;
    bool GetTaskA5(prof_start_para_t &starsProfStartPara) const;
};
class AiCoreTask {
public:
    bool GetTask(uint16_t *eventPtr, prof_start_para_t &aiCoreProfStartPara) const;
};

using TsHwtsProfileConfigT = struct TagTsHwtsProfileConfig {
    uint32_t tag;           // bit0=0 enable immediately; bit0=1 enable delay
};
class HwtsTask {
public:
    bool GetTask(prof_start_para_t &hwtsProfStartPara) const;
};

using TsL2CacheProfileConfigT = struct TagTsL2CacheProfileConfig {
    uint32_t eventNum;
    uint32_t event[PMU_EVENT_MAX_NUM];
};
class L2CacheTask {
public:
    bool GetTask(uint16_t *eventPtr, prof_start_para_t &l2CacheProfStartPara) const;
    bool GetL2cacheEvict(prof_start_para_t &l2CacheProfStartPara, uint16_t *l2EventPtr) const;
};

class HccsTask {
public:
    bool GetTask(prof_start_para_t &l2CacheProfStartPara) const;
};
class AicpuTask {
public:
    void GetTask(prof_start_para_t &aicpuProfStartPara) const;
};

void AicpuTask::GetTask(prof_start_para_t &aicpuProfStartPara) const
{
    aicpuProfStartPara.channel_type = PROF_PERIPHERAL_TYPE;
    aicpuProfStartPara.sample_period = AICPU_SAMPLE_PERIOD;
    aicpuProfStartPara.real_time = PROF_REAL;
    aicpuProfStartPara.user_data = nullptr;
    aicpuProfStartPara.user_data_size = 0;
}

bool InstrProfTask::GetTask(int mode, prof_start_para_t &instrProfStartPara) const
{
    uint32_t instrConfigSize = sizeof(InstrProfileConfigT);
    auto *configPtrInstrProf = static_cast<InstrProfileConfigT *>(malloc(instrConfigSize));
    if (configPtrInstrProf == nullptr) {
        ERROR_LOG("Can not get user data pointer while getting instr profile task");
        return false;
    }
    instrProfStartPara.user_data = nullptr;
    std::fill_n(reinterpret_cast<char *>(configPtrInstrProf), instrConfigSize, 0);

    configPtrInstrProf->period = INSTR_PROF_PERIOD;
    configPtrInstrProf->biuPcSamplingMode = static_cast<uint32_t>(mode);
    configPtrInstrProf->groupType = 0;
    configPtrInstrProf->groupNo = 0;

    instrProfStartPara.channel_type = PROF_TS_TYPE;
    instrProfStartPara.sample_period = 0;
    instrProfStartPara.real_time = PROF_REAL;
    instrProfStartPara.user_data = configPtrInstrProf;
    instrProfStartPara.user_data_size = instrConfigSize;

    return true;
}

bool FftsA5Task::GetTask(uint16_t *aicEventPtr, uint16_t *aivEventPtr, prof_start_para_t &fftsA5ProfStartPara) const
{
    uint32_t fftsA5ConfigSize = sizeof(FftsA5ProfileConfigT);
    auto *configPtrFfts = static_cast<FftsA5ProfileConfigT *>(malloc(fftsA5ConfigSize));
    if (configPtrFfts == nullptr) {
        ERROR_LOG("Can not get user data pointer while getting ffts task");
        return false;
    }
    fftsA5ProfStartPara.user_data = nullptr;
    std::fill_n(reinterpret_cast<char *>(configPtrFfts), fftsA5ConfigSize, 0);

    configPtrFfts->cfgMode = FFTS_PROF_CFG_MODE;
    configPtrFfts->blockShinkFlag = FFTS_PROF_BLOCK_SHINK_DISABLE;
    // Negotiates with the RTS to enable the use-profile-mode register when kernelscale is enable.
    configPtrFfts->aicScale = useProfileMode ? FFTS_PROF_AIC_SCALE_PARTIAL : FFTS_PROF_AIC_SCALE_ALL;
    configPtrFfts->aiEventCfg[0].period = PROF_CFG_PERIOD;
    configPtrFfts->aiEventCfg[0].eventNum = PMU_EVENT_MAX_NUM_A5;
    configPtrFfts->aiEventCfg[0].type = FFTS_PROF_CFG_TYPE;
    configPtrFfts->aiEventCfg[0].coreMask = FFTS_PROF_CFG_CORE_MASK;

    configPtrFfts->aiEventCfg[1].period = PROF_CFG_PERIOD;
    configPtrFfts->aiEventCfg[1].eventNum = PMU_EVENT_MAX_NUM_A5;
    configPtrFfts->aiEventCfg[1].type = FFTS_PROF_CFG_TYPE;
    configPtrFfts->aiEventCfg[1].coreMask = FFTS_PROF_CFG_CORE_MASK;

    for (size_t i = 0 ; i < PMU_EVENT_MAX_NUM_A5 ; ++i, ++aicEventPtr, ++aivEventPtr) {
        DEBUG_LOG("PMU event %zu, aic: %d, aiv: %d", i, *aicEventPtr, *aivEventPtr);
        configPtrFfts->aiEventCfg[0].event[i] = *aicEventPtr;
        configPtrFfts->aiEventCfg[1].event[i] = *aivEventPtr;
    }

    fftsA5ProfStartPara.channel_type = PROF_TS_TYPE;
    fftsA5ProfStartPara.sample_period = 0;
    fftsA5ProfStartPara.real_time = PROF_REAL;
    fftsA5ProfStartPara.user_data = configPtrFfts;
    fftsA5ProfStartPara.user_data_size = fftsA5ConfigSize;
    return true;
}

bool HccsTask::GetTask(prof_start_para_t &l2CacheProfStartPara) const
{
    l2CacheProfStartPara.channel_type = PROF_PERIPHERAL_TYPE;
    l2CacheProfStartPara.sample_period = static_cast<unsigned int>(10);
    l2CacheProfStartPara.real_time = PROF_REAL;
    l2CacheProfStartPara.user_data = nullptr;
    l2CacheProfStartPara.user_data_size = 0;
    return true;
}

bool FftsTask::GetTask(uint16_t *aicEventPtr, uint16_t *aivEventPtr, prof_start_para_t &fftsProfStartPara) const
{
    uint32_t fftsConfigSize = sizeof(FftsProfileConfigT);
    auto *configPtrFfts = static_cast<FftsProfileConfigT *>(malloc(fftsConfigSize));
    if (configPtrFfts == nullptr) {
        ERROR_LOG("Can not get user data pointer while getting ffts task");
        return false;
    }
    fftsProfStartPara.user_data = nullptr;
    std::fill_n(reinterpret_cast<char *>(configPtrFfts), fftsConfigSize, 0);

    configPtrFfts->cfgMode = FFTS_PROF_CFG_MODE;
    for (int i = 0; i < 2; i++) {
        configPtrFfts->aiEventCfg[i].period = PROF_CFG_PERIOD;
        configPtrFfts->aiEventCfg[i].event_num = PMU_EVENT_MAX_NUM;
        configPtrFfts->aiEventCfg[i].type = FFTS_PROF_CFG_TYPE;
        configPtrFfts->aiEventCfg[i].core_mask = FFTS_PROF_CFG_CORE_MASK;
    }

    // Negotiates with the RTS to enable the use-profile-mode register.
    // The last bit of the high-order 16 bits of the tag is used to determine
    // whether the use-profile-mode register is enabled
    if (useProfileMode) {
        configPtrFfts->tag = 0x10000;
        DEBUG_LOG("Successful set kernelScale!");
    }

    for (size_t i = 0 ; i < PMU_EVENT_MAX_NUM ; ++i, ++aicEventPtr, ++aivEventPtr) {
        DEBUG_LOG("PMU event %zu, aic: %d, aiv: %d", i, *aicEventPtr, *aivEventPtr);
        configPtrFfts->aiEventCfg[0].event[i] = *aicEventPtr;
        configPtrFfts->aiEventCfg[1].event[i] = *aivEventPtr;
    }

    fftsProfStartPara.channel_type = PROF_TS_TYPE;
    fftsProfStartPara.sample_period = PROF_CFG_PERIOD;
    fftsProfStartPara.real_time = PROF_REAL;
    fftsProfStartPara.user_data = configPtrFfts;
    fftsProfStartPara.user_data_size = fftsConfigSize;
    return true;
}

bool StarsTask::GetTask(prof_start_para_t &starsProfStartPara) const
{
    uint32_t starsConfigSize = sizeof(StarsSocLogConfigT);
    auto *starsConfigPtr = static_cast<StarsSocLogConfigT *>(malloc(starsConfigSize));
    starsProfStartPara.user_data = nullptr;
    if (starsConfigPtr == nullptr) {
        ERROR_LOG("Can not get user data pointer while getting stars task");
        return false;
    }
    std::fill_n(reinterpret_cast<char *>(starsConfigPtr), starsConfigSize, 0);

    starsConfigPtr->acsq_task = STARS_ENABLE_FLAG;
    starsConfigPtr->ffts_thread_task = STARS_ENABLE_FLAG;
    starsConfigPtr->acc_pmu = STARS_ENABLE_FLAG;

    starsProfStartPara.channel_type = PROF_TS_TYPE;
    starsProfStartPara.sample_period = PROF_CFG_PERIOD;
    starsProfStartPara.real_time = PROF_REAL;
    starsProfStartPara.user_data = starsConfigPtr;
    starsProfStartPara.user_data_size = starsConfigSize;
    return true;
}

bool StarsTask::GetTaskA5(prof_start_para_t &starsProfStartPara) const
{
    uint32_t starsConfigSize = sizeof(StarsA5SocLogConfigT);
    auto *starsConfigPtr = static_cast<StarsA5SocLogConfigT *>(malloc(starsConfigSize));
    starsProfStartPara.user_data = nullptr;
    if (starsConfigPtr == nullptr) {
        ERROR_LOG("Can not get user data pointer while getting stars task");
        return false;
    }
    std::fill_n(reinterpret_cast<char *>(starsConfigPtr), starsConfigSize, 0);

    starsConfigPtr->acsq_task = STARS_ENABLE_FLAG;
    starsConfigPtr->ffts_thread_task = STARS_ENABLE_FLAG;
    starsConfigPtr->acc_pmu = STARS_ENABLE_FLAG;

    starsProfStartPara.channel_type = PROF_TS_TYPE;
    starsProfStartPara.sample_period = PROF_CFG_PERIOD;
    starsProfStartPara.real_time = PROF_REAL;
    starsProfStartPara.user_data = starsConfigPtr;
    starsProfStartPara.user_data_size = starsConfigSize;
    return true;
}

bool AiCoreTask::GetTask(uint16_t *eventPtr, prof_start_para_t &aiCoreProfStartPara) const
{
    uint32_t aiCoreConfigSize = sizeof(TsAiCoreProfileConfigT);
    auto *configPtr = static_cast<TsAiCoreProfileConfigT *>(malloc(aiCoreConfigSize));
    aiCoreProfStartPara.user_data = nullptr;
    if (configPtr == nullptr) {
        ERROR_LOG("Can not get user data pointer while getting aicore task");
        return false;
    }
    std::fill_n(reinterpret_cast<char *>(configPtr), aiCoreConfigSize, 0);

    configPtr->tag = AICORE_TAG;
    configPtr->type = AICORE_TASK_BASE_TYPE;
    configPtr->eventNum = PMU_EVENT_MAX_NUM;
    for (size_t i = 0 ; i < PMU_EVENT_MAX_NUM ; ++i, ++eventPtr) {
        DEBUG_LOG("aicore event %zu, pmu: %d", i, *eventPtr);
        configPtr->event[i] = *eventPtr;
    }

    aiCoreProfStartPara.channel_type = PROF_TS_TYPE;
    aiCoreProfStartPara.sample_period = PROF_CFG_PERIOD;
    aiCoreProfStartPara.real_time = PROF_REAL;
    aiCoreProfStartPara.user_data = configPtr;
    aiCoreProfStartPara.user_data_size = aiCoreConfigSize;
    return true;
}

bool HwtsTask::GetTask(prof_start_para_t &hwtsProfStartPara) const
{
    uint32_t hwtsConfigSize = sizeof(TsHwtsProfileConfigT);
    auto *configPtr = static_cast<TsHwtsProfileConfigT *>(malloc(hwtsConfigSize));
    hwtsProfStartPara.user_data = nullptr;
    if (configPtr == nullptr) {
        ERROR_LOG("Can not get user data pointer while getting hwts task");
        return false;
    }
    std::fill_n(reinterpret_cast<char *>(configPtr), hwtsConfigSize, 0);

    configPtr->tag = HWTS_TAG;
    hwtsProfStartPara.channel_type = PROF_TS_TYPE;
    hwtsProfStartPara.sample_period = PROF_CFG_PERIOD;
    hwtsProfStartPara.real_time = PROF_REAL;
    hwtsProfStartPara.user_data = configPtr;
    hwtsProfStartPara.user_data_size = hwtsConfigSize;
    return true;
}

bool L2CacheTask::GetTask(uint16_t *eventPtr, prof_start_para_t &l2CacheProfStartPara) const
{
    uint32_t l2CacheConfigSize = sizeof(TsL2CacheProfileConfigT);
    auto *configPtr = static_cast<TsL2CacheProfileConfigT *>(malloc(l2CacheConfigSize));
    l2CacheProfStartPara.user_data = nullptr;
    if (configPtr == nullptr) {
        ERROR_LOG("Can not get user data pointer while getting l2cache task");
        return false;
    }
    std::fill_n(reinterpret_cast<char *>(configPtr), l2CacheConfigSize, 0);

    configPtr->eventNum = PMU_EVENT_MAX_NUM;
    for (size_t i = 0 ; i < PMU_EVENT_MAX_NUM ; ++i, ++eventPtr) {
        DEBUG_LOG("l2cache event %zu, pmu: %d", i, *eventPtr);
        configPtr->event[i] = *eventPtr;
    }

    l2CacheProfStartPara.channel_type = PROF_TS_TYPE;
    l2CacheProfStartPara.sample_period = PROF_CFG_PERIOD;
    l2CacheProfStartPara.real_time = PROF_REAL;
    l2CacheProfStartPara.user_data = configPtr;
    l2CacheProfStartPara.user_data_size = l2CacheConfigSize;
    return true;
}

bool L2CacheTask::GetL2cacheEvict(prof_start_para_t &l2CacheProfStartPara, uint16_t *l2EventPtr) const
{
    uint32_t l2CacheConfigSize = sizeof(TsL2CacheProfileConfigT) + 8 * sizeof(uint32_t);
    auto *configPtr = static_cast<TsL2CacheProfileConfigT *>(malloc(l2CacheConfigSize));
    l2CacheProfStartPara.user_data = nullptr;
    if (configPtr == nullptr) {
        ERROR_LOG("Can not get user data pointer while getting l2cache task");
        return false;
    }
    std::fill_n(reinterpret_cast<char *>(configPtr), l2CacheConfigSize, 0);
    std::copy(l2EventPtr, l2EventPtr + PMU_EVENT_MAX_NUM, configPtr->event);
    uint32_t *eventPtr = configPtr->event;
    configPtr->eventNum = PMU_EVENT_MAX_NUM;
    for (size_t i = 0 ; i < PMU_EVENT_MAX_NUM ; ++i, ++eventPtr) {
        DEBUG_LOG("l2cache event %zu, pmu:0x%X", i, *eventPtr);
        configPtr->event[i] = *eventPtr;
    }

    l2CacheProfStartPara.channel_type = PROF_TS_TYPE;
    l2CacheProfStartPara.sample_period = PROF_CFG_PERIOD;
    l2CacheProfStartPara.real_time = PROF_REAL;
    l2CacheProfStartPara.user_data = configPtr;
    l2CacheProfStartPara.user_data_size = l2CacheConfigSize;
    return true;
}

void ProfTask::ChannelRead()
{
    map<unsigned int, std::string> channelFileMap = {
        {CHANNEL_FFTS_PROFILE_BUFFER_TASK, "DeviceProf"},
        {CHANNEL_AICORE, "DeviceProf"},
        {CHANNEL_STARS_SOC_LOG_BUFFER, "duration.bin"},
        {CHANNEL_HWTS_LOG, "duration.bin"},
        {CHANNEL_TSFW_L2,  "L2Cache.bin"},
        {CHANNEL_AICPU,    "aicpu.bin"},
        {CHANNEL_HCCS,    "hccs.bin"}
    };

    vector<char> outBuf(PROF_CHANNEL_BUFFER_SIZE, 0);
    vector<prof_poll_info_t> channels(PROF_CHANNEL_NUM);
    InstrChnReadCtrl instrChnReadController;
    do {
        int ret = prof_channel_poll_origin(channels.data(), PROF_CHANNEL_NUM, 1);
        for (int i = 0; i < std::min(ret, PROF_CHANNEL_NUM); i++) {
            std::fill_n(&outBuf[0], PROF_CHANNEL_BUFFER_SIZE, 0);
            int curLen = prof_channel_read_origin(channels[i].device_id, channels[i].channel_id,
                                                  &outBuf[0], PROF_CHANNEL_BUFFER_SIZE);
            string dumpPath = ProfDataCollect::GetAicoreOutputPath(static_cast<int32_t>(channels[i].device_id));
            if (dumpPath.empty()) {
                ERROR_LOG("Can not find dump path to generate bin file.");
                continue;
            }
            if (WriteInstrChannelData(dumpPath, static_cast<InstrChannel>(channels[i].channel_id),
                &outBuf[0], curLen, instrChnReadController)) {
                continue;
            }
            auto channelIter = channelFileMap.find(channels[i].channel_id);
            if ((channelIter == channelFileMap.end()) || (curLen <= 0)) {
                continue;
            }
            string fileName = channelIter->second;
            if (channels[i].channel_id == CHANNEL_FFTS_PROFILE_BUFFER_TASK ||
                channels[i].channel_id == CHANNEL_AICORE) {
                fileName = fileName + to_string(ProfDataCollect::GetDeviceReplayCount(
                    static_cast<int32_t>(channels[i].device_id)) + 1) + ".bin";
            }
            string binFile = JoinPath({dumpPath, fileName});
            if (WriteBinary(binFile, &outBuf[0], curLen, std::ios::app) == 0) {
                WARN_LOG("Write bin failed, bin is %s", binFile.c_str());
            } else {
                DEBUG_LOG("Write bin success, bin is %s, curLen is %d", binFile.c_str(), curLen);
            }
        }
    } while (profRunning_);
}

bool ProfTaskOfA5::WriteInstrChannelData(const std::string &prefixName, InstrChannel channelId,
    const char *data, int validLen, InstrChnReadCtrl &instrChnReadController)
{
    if (instrProfChn_.count(channelId) == 0UL) {
        return false;
    }
    DEBUG_LOG("Write instr prof channel id:%d", static_cast<int>(channelId));
    // 单文件不超过1G
    if (instrChnReadController.InstrProfReadSize > MAX_BIN_FILE_SIZE - sizeof(InstrProfHeadInfoT) -
        static_cast<uint64_t>(PROF_CHANNEL_BUFFER_SIZE)) {
        instrChnReadController.splitFileNum += 1;
        instrChnReadController.InstrProfReadSize = 0;
    }
    // 根据当前 InstrProf 状态决定写入的文件名
    std::string binName;
    if (instrProfState_ == InstrProfState::PC_SAMPLING) {
        binName = "pcSampling.bin." + std::to_string(instrChnReadController.splitFileNum);
    } else if (instrProfState_ == InstrProfState::TIMELINE) {
        binName = "timeline.bin." + std::to_string(instrChnReadController.splitFileNum);
    } else {
        return true;
    }
    std::string binFile = JoinPath({prefixName, binName});
    InstrProfHeadInfoT instrProfHeadInfo;
    instrProfHeadInfo.coreId = static_cast<uint16_t>(instrProfChn_[channelId].grpId);
    instrProfHeadInfo.coreType = static_cast<uint16_t>(instrProfChn_[channelId].grpType);
    instrProfHeadInfo.validLen = static_cast<uint32_t>(validLen);

    // 先写入头信息再写入数据
    if (WriteBinary(binFile, reinterpret_cast<char *>(&instrProfHeadInfo), sizeof(InstrProfHeadInfoT),
        std::ios::app) == 0UL) {
        DEBUG_LOG("Write instr head info failed, bin is %s", binName.c_str());
        return true;
    } else {
        instrChnReadController.InstrProfReadSize += sizeof(InstrProfHeadInfoT);
        DEBUG_LOG("Write instr head info success, bin is %s, valid len is %u", binName.c_str(),
                  instrProfHeadInfo.validLen);
    }
    if (WriteBinary(binFile, data, PROF_CHANNEL_BUFFER_SIZE, std::ios::app) == 0UL) {
        DEBUG_LOG("Write instr data info failed, bin is %s", binName.c_str());
    } else {
        instrChnReadController.InstrProfReadSize += static_cast<uint64_t>(PROF_CHANNEL_BUFFER_SIZE);
        DEBUG_LOG("Write instr data info success, bin is %s, size is %u", binName.c_str(),
                  instrChnReadController.InstrProfReadSize);
    }
    return true;
}

bool ProfTaskOfA5::StartInstrProfTask(int mode)
{
    InstrProfTask instrProfTask;
    prof_start_para_t instrProfStartPara;
    if (!instrProfTask.GetTask(mode, instrProfStartPara)) {
        return false;
    }
    for (auto const &it : instrProfChn_) {
        int ret = prof_drv_start_origin(deviceId_, static_cast<uint32_t>(it.first), &instrProfStartPara);
        if (ret != 0) {
            DEBUG_LOG("Failed to start instr profiling channel:%d, return code:%d", static_cast<uint32_t>(it.first), ret);
        }
    }
    free(instrProfStartPara.user_data);
    return true;
}

bool ProfTaskOfA5::StartFFTSTask(uint32_t replayCount)
{
    FftsA5Task fftsTask;
    if (replayCount >= EVENT_MAX_NUM_A5 / PMU_EVENT_MAX_NUM_A5) {
        return false;
    }
    uint16_t *aicEventPtr = profTaskConfig_.aicPmu + replayCount * PMU_EVENT_MAX_NUM_A5;
    uint16_t *aivEventPtr = profTaskConfig_.aivPmu + replayCount * PMU_EVENT_MAX_NUM_A5;
    prof_start_para_t fftsProfStartPara;
    if (profTaskConfig_.useProfileMode) {
        fftsTask.useProfileMode = true;
        DEBUG_LOG("Successful set kernelScale!");
    }
    if (!fftsTask.GetTask(aicEventPtr, aivEventPtr, fftsProfStartPara) ||
        prof_drv_start_origin(deviceId_, CHANNEL_FFTS_PROFILE_BUFFER_TASK, &fftsProfStartPara) != 0) {
        ERROR_LOG("Profiling channel start failed.");
        free(fftsProfStartPara.user_data);
        return false;
    }
    free(fftsProfStartPara.user_data);
    if ((replayCount == EVENT_MAX_NUM_A5 / PMU_EVENT_MAX_NUM_A5 - 1) ||
        ((aicEventPtr[PMU_EVENT_MAX_NUM_A5] == 0) && (aivEventPtr[PMU_EVENT_MAX_NUM_A5] == 0))) {
        isLastReplay_ = true;
    }
    return true;
}

bool ProfTaskOfA5::StartStarsTask()
{
    StarsTask starsTask {};
    prof_start_para_t starsProfStartPara;
    if (!starsTask.GetTaskA5(starsProfStartPara) ||
        prof_drv_start_origin(deviceId_, CHANNEL_STARS_SOC_LOG_BUFFER, &starsProfStartPara) != 0) {
        ERROR_LOG("Profiling channel start failed.");
        free(starsProfStartPara.user_data);
        return false;
    }
    free(starsProfStartPara.user_data);
    isStarsStart_ = true;
    return true;
}

bool ProfTaskOfA5::Start(uint32_t replayCount, bool hasSimt)
{
    profRunning_ = true;
    hasSimt_ = hasSimt;
    // 状态机: IDLE 态下根据配置决定进入 PC_SAMPLING 或 TIMELINE
    if (instrProfState_ == InstrProfState::IDLE &&
        ProfConfig::Instance().IsPCSamplingEnabled() && hasSimt_) {
        instrProfState_ = InstrProfState::PC_SAMPLING;
        return StartInstrProfTask(INSTR_PROF_MODE_PC_SAMPLING);
    }
    // IDLE 或 TIMELINE 态下，如果 timeline 使能则启动 timeline 采集
    if ((instrProfState_ == InstrProfState::IDLE || instrProfState_ == InstrProfState::TIMELINE) &&
        ProfConfig::Instance().IsTimelineEnabled()) {
        instrProfState_ = InstrProfState::TIMELINE;
        return StartInstrProfTask(INSTR_PROF_MODE_BIU_PERF);
    }
    // DONE 或未启用 InstrProf → 正常 FFTS 重放
    if (!StartFFTSTask(replayCount)) {
        return false;
    }

    // save the last replay duration.bin
    if (!isStarsStart_ && isLastReplay_) {
        return StartStarsTask();
    }
    return true;
}

// 关闭所有 InstrProf 通道的辅助方法
void ProfTaskOfA5::StopInstrProfChannels()
{
    for (auto const &it : instrProfChn_) {
        prof_stop_origin(deviceId_, static_cast<uint32_t>(it.first));
    }
}

void ProfTaskOfA5::Stop()
{
    prof_stop_origin(deviceId_, CHANNEL_FFTS_PROFILE_BUFFER_TASK);
    if (isStarsStart_) {
        prof_stop_origin(deviceId_, CHANNEL_STARS_SOC_LOG_BUFFER);
    }
    // 状态机转移: PC_SAMPLING → TIMELINE(如果timeline使能) 或 DONE
    if (instrProfState_ == InstrProfState::PC_SAMPLING) {
        StopInstrProfChannels();
        instrProfState_ = ProfConfig::Instance().IsTimelineEnabled()
            ? InstrProfState::TIMELINE : InstrProfState::DONE;
    } else if (instrProfState_ == InstrProfState::TIMELINE) {
        // TIMELINE → DONE
        StopInstrProfChannels();
        instrProfState_ = InstrProfState::DONE;
    }
    profRunning_ = false;
}

bool ProfTaskOf910B::StartStarsTask()
{
    StarsTask starsTask;
    prof_start_para_t starsProfStartPara {};
    if (!starsTask.GetTask(starsProfStartPara) ||
        prof_drv_start_origin(deviceId_, CHANNEL_STARS_SOC_LOG_BUFFER, &starsProfStartPara) != 0) {
        ERROR_LOG("Profiling channel start failed.");
        free(starsProfStartPara.user_data);
        return false;
    }
    free(starsProfStartPara.user_data);
    isStarsStart_ = true;
    return true;
}

bool ProfTaskOf910B::StartFFTSTask(uint32_t replayCount)
{
    FftsTask fftsTask {};
    if (replayCount > (EVENT_MAX_NUM / PMU_EVENT_MAX_NUM - 1)) {
        return false;
    }
    uint16_t *aicEventPtr = profTaskConfig_.aicPmu + replayCount * PMU_EVENT_MAX_NUM;
    uint16_t *aivEventPtr = profTaskConfig_.aivPmu + replayCount * PMU_EVENT_MAX_NUM;
    prof_start_para_t fftsProfStartPara {};
    if (profTaskConfig_.useProfileMode) {
        fftsTask.useProfileMode = true;
    }
    if (!fftsTask.GetTask(aicEventPtr, aivEventPtr, fftsProfStartPara) ||
        prof_drv_start_origin(deviceId_, CHANNEL_FFTS_PROFILE_BUFFER_TASK, &fftsProfStartPara) != 0) {
        ERROR_LOG("Profiling channel start failed.");
        free(fftsProfStartPara.user_data);
        return false;
    }
    free(fftsProfStartPara.user_data);
    if ((replayCount == EVENT_MAX_NUM / PMU_EVENT_MAX_NUM - 1) ||
        ((aicEventPtr[PMU_EVENT_MAX_NUM] == 0) && (aivEventPtr[PMU_EVENT_MAX_NUM] == 0))) {
        isLastReplay_ = true;
    }
    return true;
}

bool ProfTaskOf910B::StartTSFWTask()
{
    if (isL2CacheStart_) {
        return true;
    }
    L2CacheTask l2CacheTask;
    prof_start_para_t l2CacheProfStartPara;
    uint16_t *l2EventPtr = profTaskConfig_.l2CachePmu;
    l2CacheTask.GetL2cacheEvict(l2CacheProfStartPara, l2EventPtr);
    if (prof_drv_start_origin(deviceId_, CHANNEL_TSFW_L2, &l2CacheProfStartPara) != 0) {
        ERROR_LOG("Profiling channel start failed, channel is %u.", static_cast<uint32_t>(CHANNEL_TSFW_L2));
        free(l2CacheProfStartPara.user_data);
        return false;
    }
    free(l2CacheProfStartPara.user_data);
    isL2CacheStart_ = true;
    return true;
}

bool ProfTaskOf910B::StartHCCSTask()
{
    if (isHccs_) {
        return true;
    }
    HccsTask hccsTask;
    prof_start_para_t profStartPara;
    hccsTask.GetTask(profStartPara);
    if (prof_drv_start_origin(deviceId_, CHANNEL_HCCS, &profStartPara) != 0) {
        ERROR_LOG("Profiling channel start failed, channel is %d.", static_cast<uint32_t>(CHANNEL_HCCS));
        free(profStartPara.user_data);
        return false;
    }
    free(profStartPara.user_data);
    isHccs_ = true;
    return true;
}

bool ProfTaskOf910B::Start(uint32_t replayCount, bool hasSimt)
{
    profRunning_ = true;
    if (profTaskConfig_.l2CachePmu[0] != 0) {
        profTaskConfig_.l2CachePmu[0] = 0;
        if (!StartTSFWTask()) {
            WARN_LOG("Profiling l2 channel start failed!.");
            return false;
        }
        return true;
    }
    if (!StartFFTSTask(replayCount)) {
        return false;
    }
    // aicpu channel 143 can only be opened by MC2
    if (replayCount == 0 && KernelContext::Instance().GetMC2Flag()) {
        AicpuTask aicpuTask;
        prof_start_para_t aicpuProfStartPara;
        aicpuTask.GetTask(aicpuProfStartPara);
        if (prof_drv_start_origin(deviceId_, CHANNEL_AICPU, &aicpuProfStartPara) != 0) {
            ERROR_LOG("Profiling channel start failed.");
            return false;
        }
    }

    // save the last replay duration.bin
    if (!isStarsStart_ && isLastReplay_) {
        return StartStarsTask();
    }
    return true;
}

void ProfTaskOf910B::Stop()
{
    prof_stop_origin(deviceId_, CHANNEL_FFTS_PROFILE_BUFFER_TASK);
    if (isStarsStart_) {
        prof_stop_origin(deviceId_, CHANNEL_STARS_SOC_LOG_BUFFER);
    }
    if (isLastReplay_ && KernelContext::Instance().GetMC2Flag()) {
        prof_stop_origin(deviceId_, CHANNEL_AICPU);
    }
    if (isL2CacheStart_) {
        prof_stop_origin(deviceId_, CHANNEL_TSFW_L2);
    }
    if (isHccs_) {
        prof_stop_origin(deviceId_, CHANNEL_HCCS);
    }
    profRunning_ = false;
}

bool ProfTaskOf310P::StartHwtsTask()
{
    HwtsTask hwtsTask;
    prof_start_para_t hwtsProfStartPara {};
    if (!hwtsTask.GetTask(hwtsProfStartPara) ||
        prof_drv_start_origin(deviceId_, CHANNEL_HWTS_LOG, &hwtsProfStartPara) != 0) {
        ERROR_LOG("Profiling channel start failed.");
        free(hwtsProfStartPara.user_data);
        return false;
    }
    free(hwtsProfStartPara.user_data);
    isHwtsStart_ = true;
    return true;
}

bool ProfTaskOf310P::Start(uint32_t replayCount, bool hasSimt)
{
    profRunning_ = true;

    if (replayCount >= EVENT_MAX_NUM / PMU_EVENT_MAX_NUM) {
        return false;
    }
    uint16_t *aicEventPtr = profTaskConfig_.aicPmu + replayCount * PMU_EVENT_MAX_NUM;
    if (profTaskConfig_.useProfileMode) {
        DEBUG_LOG("KernelScale param is only use for Ascend910B!");
    }
    if (aicEventPtr[0] != 0 || replayCount == 0) {
        AiCoreTask aiCoreTask {};
        prof_start_para_t aiCoreProfStartPara;
        if (!aiCoreTask.GetTask(aicEventPtr, aiCoreProfStartPara) ||
            prof_drv_start_origin(deviceId_, CHANNEL_AICORE, &aiCoreProfStartPara) != 0) {
            ERROR_LOG("Profiling channel start failed.");
            free(aiCoreProfStartPara.user_data);
            return false;
        }
        free(aiCoreProfStartPara.user_data);
        isAiCoreStart_ = true;
    }
    // save the last replay duration.bin
    if (!isHwtsStart_ && (aicEventPtr[PMU_EVENT_MAX_NUM] == 0)) {
        if (!StartHwtsTask()) {
            return false;
        }
    }

    if (replayCount == 0 && profTaskConfig_.l2CachePmu[0] != 0) {
        L2CacheTask l2CacheTask {};
        prof_start_para_t l2CacheProfStartPara {};
        if (!l2CacheTask.GetTask(profTaskConfig_.l2CachePmu, l2CacheProfStartPara) ||
            prof_drv_start_origin(deviceId_, CHANNEL_TSFW_L2, &l2CacheProfStartPara) != 0) {
            ERROR_LOG("Profiling channel start failed.");
            free(l2CacheProfStartPara.user_data);
            return false;
        }
        free(l2CacheProfStartPara.user_data);
        isL2CacheStart_ = true;
    }
    return true;
}

void ProfTaskOf310P::Stop()
{
    if (isAiCoreStart_) {
        prof_stop_origin(deviceId_, CHANNEL_AICORE);
    }
    if (isHwtsStart_) {
        prof_stop_origin(deviceId_, CHANNEL_HWTS_LOG);
    }
    if (isL2CacheStart_) {
        prof_stop_origin(deviceId_, CHANNEL_TSFW_L2);
    }
    profRunning_ = false;
}

std::unique_ptr<ProfTask> ProfTaskFactory::Create()
{
    uint32_t deviceId = static_cast<uint32_t>(DeviceContext::GetRunningDeviceId());
    std::string socVersion = DeviceContext::Local().GetSocVersion();
    std::unique_ptr<ProfTask> taskPtr;
    if (StartsWith(socVersion, "Ascend310P")) {
        taskPtr = std::unique_ptr<ProfTaskOf310P>{
            new ProfTaskOf310P(ProfConfig::Instance().GetConfig(), deviceId)};
    } else if (StartsWith(socVersion, "Ascend910B")) {
        taskPtr = std::unique_ptr<ProfTaskOf910B>{
            new ProfTaskOf910B(ProfConfig::Instance().GetConfig(), deviceId)};
    } else if (StartsWith(socVersion, "Ascend950")) {
        taskPtr = std::unique_ptr<ProfTaskOfA5>{
            new ProfTaskOfA5(ProfConfig::Instance().GetConfig(), deviceId)};
    } else {
        taskPtr = std::unique_ptr<ProfTask>{
            new ProfTaskOf910B(ProfConfig::Instance().GetConfig(), deviceId)};
    }
    return taskPtr;
}
