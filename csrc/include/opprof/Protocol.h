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
#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <cstddef>
#include <cstdint>
#include "BasicDefs.h"
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

struct MstxProfConfig {
    bool isMstxEnable {false};
    char mstxEnabledMessage[NAME_MAX_LENGTH] {'\0'}; // op_profiling/common/defs.h:MAX_KERNEL_NAME_LENGTH + 1
};

struct ProfDataPathConfig {
    char kernelName[NAME_MAX_LENGTH];
    uint32_t deviceId;
};
#pragma pack(4)
struct CollectLogStart {
    char outputPath[PATH_MAX_LENGTH];
    char kernelName[NAME_MAX_LENGTH];
};

struct ProfPacketHead {
    ProfPacketType type;
    uint32_t length;
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
struct MessageOfProfConfig {
    MstxProfConfig mstxProfConfig { };
    uint32_t replayCount {UINT32_INVALID};
    uint32_t dbiFlag {0};
    uint16_t profWarmUpTimes {0};
    uint16_t aicPmu[EVENT_MAX_NUM]{};
    uint16_t aivPmu[EVENT_MAX_NUM]{};
    uint16_t l2CachePmu[EVENT_MAX_NUM]{};
    uint8_t replayMode {0};
    char instrTimelinePipe[NAME_MAX_LENGTH] {};
    bool useProfileMode {false};
    bool killAdvance {false};
    bool isDeviceToSimulator {false};
    bool isSimulator {false};
    bool pmSamplingEnable {false};
};

#endif // __PROTOCOL_H__
