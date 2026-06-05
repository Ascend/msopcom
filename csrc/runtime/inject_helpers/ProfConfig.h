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

constexpr uint32_t PMU_EVENT_MAX_NUM = 8U;
constexpr uint32_t EVENT_MAX_NUM = 96U;
constexpr uint32_t PMU_EVENT_MAX_NUM_A5 = 10U;
constexpr uint32_t EVENT_MAX_NUM_A5 = 60U;
constexpr uint32_t UINT32_INVALID = UINT32_MAX;
constexpr uint16_t PATH_MAX_LENGTH = 4096U;
constexpr uint16_t NAME_MAX_LENGTH = 1024U;
constexpr uint16_t WARMUP_MAX_TIMES = 500;
constexpr uint16_t INSTR_PROF_MODE_BIU_PERF = 0;
constexpr uint16_t INSTR_PROF_MODE_PC_SAMPLING = 1;
constexpr uint16_t INSTR_PROF_MEMSIZE = 8;
constexpr char const *OPERAND_RECORD = "OperandRecord.bin";
constexpr char const *PCOFFSET_RECORD = "PcOffset.bin";
constexpr char const *START_STUB_COMPILER_ARGS = "--kernel-bounded-pcstb-mode";
constexpr char const *CAMODEL_SOC_VERSION_ENV = {"CAMODEL_SOC_VERSION"};
constexpr char const *DEVICE_TO_SIMULATOR = {"DEVICE_TO_SIMULATOR"};
constexpr char const *IS_SIMULATOR_ENV = {"IS_SIMULATOR_ENV"};
constexpr char const *DEVICE_PROF_DUMP_PATH_ENV = {"DEVICE_PROF_DUMP_PATH"};
constexpr char const *MSOPPROF_EXE_PATH_ENV = {"MSOPPROF_EXE_PATH"};
constexpr uint64_t BLOCK_GAP = 64U; // 每个block间需要间隔64B
constexpr uint64_t MAX_BLOCK = 108U; // 最大108个block
constexpr uint64_t SIMT_THREAD_GAP = 64U; // 每个SIMT thread间需要间隔64B
constexpr uint32_t MAX_THREAD_NUM = 2048U; // SIMT thread个数
constexpr uint64_t MAX_BLOCK_DATA_SIZE = 100U * 1024 * 1024; // 每个block存储100MB数据
constexpr uint64_t BLOCK_MEM_SIZE = MAX_BLOCK_DATA_SIZE + BLOCK_GAP; // 每个block占用内存大小
constexpr uint64_t RECORD_OVERFLOW_BIT = 1ULL << 63; // BlockHeader溢出标记位

uint64_t GetCoreNumForDbi(uint64_t blockDim);

struct MstxProfConfig {
    bool isMstxEnable {false};
    char mstxEnabledMessage[NAME_MAX_LENGTH] {'\0'}; // op_profiling/common/defs.h:MAX_KERNEL_NAME_LENGTH + 1
};

struct InstrChnReadCtrl {
    uint32_t splitFileNum = 0;
    uint32_t InstrProfReadSize = 0;
};

enum class ProfDBIType {
    AS_IS = 0, // 不插桩
    OPERAND_RECORD, // operand record桩
    MEMORY_CHART, // memory chart桩
    INSTR_PROF_START, // start桩
    INSTR_PROF_END, // end桩
    BB_COUNT // bb count桩
};

constexpr uint32_t DBI_FLAG_OPERAND_RECORD = 1U << static_cast<uint32_t>(ProfDBIType::OPERAND_RECORD);
constexpr uint32_t DBI_FLAG_MEMORY_CHART = 1U << static_cast<uint32_t>(ProfDBIType::MEMORY_CHART);
constexpr uint32_t DBI_FLAG_INSTR_PROF_START = 1U << static_cast<uint32_t>(ProfDBIType::INSTR_PROF_START);
constexpr uint32_t DBI_FLAG_INSTR_PROF_END = 1U << static_cast<uint32_t>(ProfDBIType::INSTR_PROF_END);
constexpr uint32_t DBI_FLAG_BB_COUNT = 1U << static_cast<uint32_t>(ProfDBIType::BB_COUNT);

enum class OperandType : uint8_t {
    // 浮点数据类型 - 新增浮点类型必须添加在 DATA_FLOAT_MAX 之前
    DATA_F16 = 0,
    DATA_F16X2,
    DATA_BF16,
    DATA_BF16X2,
    DATA_E4M3,
    DATA_E5M2,
    DATA_E1M2,
    DATA_E2M1,
    DATA_V2BF16,
    DATA_V2F16,
    DATA_HIF8X2,
    DATA_F8E4M3X2,
    DATA_F8E5M2X2,
    DATA_FMIX,
    DATA_HALF,
    DATA_F32,
    DATA_F32X2,
    DATA_FLOAT_MAX = DATA_F32X2, // 浮点类型结束标记，添加新类型后需更新

    // 整数数据类型
    DATA_B4,
    DATA_B8,
    DATA_B16,
    DATA_B32,
    DATA_B64,
    DATA_B128,
    DATA_S16,
    DATA_S32,
    DATA_U16,
    DATA_U32,
    DATA_S8,
    DATA_U8,
    DATA_U64,
    DATA_S64,
    DATA_SX32,
    DATA_ZX32,

    // 结束标记（非数据类型）
    END
};

struct OperandHeader {
    uint32_t magicWords;
    uint32_t reverse;
};

struct MessageOfProfConfig {
    MstxProfConfig mstxProfConfig { };
    uint32_t replayCount {UINT32_INVALID};
    uint32_t dbiFlag {0};
    uint16_t profWarmUpTimes {0};
    uint16_t aicPmu[EVENT_MAX_NUM]{};
    uint16_t aivPmu[EVENT_MAX_NUM]{};
    uint16_t l2CachePmu[EVENT_MAX_NUM]{};
    uint8_t replayMode {0};
    bool useProfileMode {false};
    bool killAdvance {false};
    bool isDeviceToSimulator {false};
    bool isSimulator {false};
    bool pmSamplingEnable {false};
};

enum class ReplayMode : uint8_t {
    KERNEL = 0,
    APPLICATION,
    RANGE,
};

// change
enum class ProfPacketType : uint32_t {
    CONFIG = 0,
    DATA_PATH,
    PROCESS_CTRL,
    DBI_DATA,
    COLLECT_START,
    PROF_FINISH,
    INSTR_LOG = 20,
    POPPED_LOG,
    ICACHE_LOG,
    MTE_LOG,
    CCU_LOG,
    INVALID,
};

#pragma pack(4)
struct OperandRecord {
    uint64_t instructions{};
    uint64_t operands{};
    uint64_t funcType{};
};

// 考虑记录条目数过多时，把length最高位置1，剩余63位记录经过桩的请求总数，count维持不变，保留可读数据
struct BlockHeader {
    uint64_t count;    // 该block记录的条目数
    uint64_t length;   // 该block已经存储的长度，不包含BlockHeader自身，device侧使用
    uint64_t ndPara;   // #3023 set_nd_para，MOV_FP类指令使用
    uint64_t loop3Para;   // #set_loop3Para_para，FIX_L0C_TO_OUT类指令使用
    uint64_t channelPara;   // #set_channel_para，FIX_L0C_TO_OUT类指令使用
    uint64_t loopSizeOuttol1;      // set_loop_size_outtol1，MOV_OUT_TO_L1_ALIGN_V2指令使用
    uint64_t loop1StrideOuttol1;   // set_loop1_stride_outtol1，MOV_OUT_TO_L1_ALIGN_V2类指令使用
    uint64_t loop2StrideOuttol1;   // set_loop2_stride_outtol1，MOV_OUT_TO_L1_ALIGN_V2类指令使用
    uint64_t loopSizeOuttoub;      // set_loop_size_outtoub，MOV_OUT_TO_L1_ALIGN_V2指令使用
    uint64_t loop1StrideOuttoub;   // set_loop1_stride_outtoub，MOV_OUT_TO_L1_ALIGN_V2指令使用
    uint64_t loop2StrideOuttoub;   // set_loop2_stride_outtoub，MOV_OUT_TO_L1_ALIGN_V2指令使用
    uint64_t loopSizeUbToOut; // #3709 set_loop_size_para_ubtout，MOV_UB_TO_OUT_ALIGN_V2指令使用
    uint64_t loop1StrideUbToOut; // #2991 set_loop1_size_para_ubtout，MOV_UB_TO_OUT_ALIGN_V2指令使用
    uint64_t loop2StrideUbToOut; // #2996 set_loop2_size_para_ubtout，MOV_UB_TO_OUT_ALIGN_V2指令使用
    uint64_t mte2NzPara;           // set_mte2_nz_para，MOV_OUT_TO_L1_MULTI_ND2NZ指令使用
    uint64_t padCntNdDma;          // set_pad_cnt_nddma，NDDMA_OUT_TO_UB指令使用
    uint64_t loop0StrideNdDma;     // set_loop0_stride_nddma，NDDMA_OUT_TO_UB指令使用
    uint64_t loop1StrideNdDma;     // set_loop1_stride_nddma，NDDMA_OUT_TO_UB指令使用
    uint64_t loop2StrideNdDma;     // set_loop2_stride_nddma，NDDMA_OUT_TO_UB指令使用
    uint64_t loop3StrideNdDma;     // set_loop3_stride_nddma，NDDMA_OUT_TO_UB指令使用
    uint64_t loop4StrideNdDma;     // set_loop4_stride_nddma，NDDMA_OUT_TO_UB指令使用
    uint64_t mte2SrcPara;          // set_mte2_src_para，LOAD_OUT_TO_L1_2Dv2指令使用
};

struct DBIDataHeader {
    uint64_t count;      // 该block记录的条目数
    uint64_t length;     // Header后紧跟的数据长度，也就是输出路径长度或者动态插桩数据长度
    uint64_t overflow;   // 缓冲区不足而未记录的数据条目数
    uint16_t blockId;
    uint8_t endFlag;    // 该path下所有block的数据都发送完成
};

struct ProfPacketHead {
    ProfPacketType type;
    uint32_t length;
};

struct ProfDataPathConfig {
    char kernelName[NAME_MAX_LENGTH];
    uint32_t deviceId;
};

struct CollectLogStart {
    char outputPath[PATH_MAX_LENGTH];
    char kernelName[NAME_MAX_LENGTH];
};

struct ProcessCtrl {
// 采集后通信请求
    struct Req {
        uint8_t done; // 是否完成采集任务
        int32_t deviceId;
    };
// 采集后通信回复
    struct Rsp {
        uint8_t termination {0}; // 是否提前结束进程
    };
};
#pragma pack()

class ProfConfig {
public:
    static ProfConfig &Instance()
    {
        static ProfConfig inst;
        return inst;
    }
    void Reset() const { Instance() = ProfConfig(); }

    void Init(const MessageOfProfConfig &profConfig) { profConfig_ = profConfig; }

    const MessageOfProfConfig &GetConfig() const { return profConfig_; }

    uint32_t GetInitReplayCount() const { return isAppReplay_ ? profConfig_.replayCount : 0U; }

    uint16_t GetWarmUpTimes() const
    {
        return profConfig_.profWarmUpTimes <= WARMUP_MAX_TIMES ? profConfig_.profWarmUpTimes : WARMUP_MAX_TIMES;
    }

    bool IsAppReplay() const { return isAppReplay_; }

    bool IsRangeReplay() const { return isRangeReplay_; }

    bool IsSimulator() const { return profConfig_.isSimulator; }

    bool IsEnablePmSampling() const { return profConfig_.pmSamplingEnable; }

    bool IsTimelineEnabled() const { return profConfig_.dbiFlag & DBI_FLAG_INSTR_PROF_END;}

    bool IsPCSamplingEnabled() const { return profConfig_.dbiFlag & DBI_FLAG_INSTR_PROF_START; }

    bool IsDbi() const
    {
        return isAppReplay_ && (profConfig_.dbiFlag & ~DBI_FLAG_INSTR_PROF_END) != 0;
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

    std::string GetOutputPathFromRemote(const std::string &kernelName, int32_t deviceId);

    bool PostNotify(ProcessCtrl::Rsp &rsp); // 采集后通信

    bool IsSimulatorLaunchedByDevice() const;

    std::string GetPluginPath(ProfDBIType pluginType = ProfDBIType::MEMORY_CHART) const;

    void SendMsg(const std::string &msg);

    std::string GetMsopprofPath() const;
private:
    ProfConfig();
    ProfConfig(const ProfConfig& p)
    {
        profConfig_ = p.profConfig_;
    }
    ProfConfig& operator=(const ProfConfig& p)
    {
        profConfig_ = p.profConfig_;
        return *this;
    }

    void InitProfConfig();
    void LoadSocVersionFromEnv();
    std::mutex communtionMx_;
    MessageOfProfConfig profConfig_ {};
    std::string socVersion_;
    bool isAppReplay_ {false};
    bool isRangeReplay_ {false};
    bool isCaLogTrans_ {false};
};
#endif // __PROF_TYPE_H__
