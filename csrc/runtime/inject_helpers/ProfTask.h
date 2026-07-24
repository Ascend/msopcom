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

#ifndef __PROF_TASK_H__
#define __PROF_TASK_H__

#include <memory>
#include <atomic>
#include <map>
#include "ProfConfig.h"
#include "ascend_hal/AscendHalOrigin.h"

/*
 * device task operation, which contains:
 * 1. 用于上板时的性能数据采集, 包括所有驱动通道采集任务.
 * 2. 此类中也包括驱动器通道的处理方法，如读通道\停止通道等。.
 *
 * limit:
 * 1. 只有为驱动通道采集任务时，才可以在该文件中添加内容。.
 *
 */
/*
 * InstrProf 采集状态机:
 * IDLE → PC_SAMPLING → TIMELINE → DONE (同时开启pcsampling和timeline时的完整流转)
 * IDLE → TIMELINE → DONE (仅开启timeline)
 * IDLE → PC_SAMPLING → DONE (仅开启pcsampling)
 * DONE 后进入正常 FFTS 重放流程
 */
enum class InstrProfState : uint8_t {
    IDLE,           // 初始态: 未开始任何 InstrProf 采集
    PC_SAMPLING,    // 正在执行 PCSampling 采集
    TIMELINE,       // 正在执行 Timeline 采集
    DONE            // InstrProf 采集全部完成
};

class ProfTask {
public:
    struct InstrChnReadCtrl {
        uint32_t splitFileNum = 0;
        uint32_t InstrProfReadSize = 0;
    };
    ProfTask(const MessageOfProfConfig &profTaskConfig, uint32_t deviceId) : profTaskConfig_(profTaskConfig),
        deviceId_(deviceId) {}
    virtual ~ProfTask() = default;
    virtual bool Start(uint32_t replayCount, bool hasSimt) {return false;};
    virtual bool WriteInstrChannelData(const std::string &prefixName, InstrChannel channelId,
        const char *outBuf, int validLen, InstrChnReadCtrl &instrChnReadController) {return false;}
    void ChannelRead();
    virtual void Stop() { };
    std::atomic<bool> profRunning_ {false};

protected:
    bool isL2CacheStart_ = false;
    bool isHccs_ = false;
    MessageOfProfConfig profTaskConfig_;
    uint32_t deviceId_;
    bool isLastReplay_ = false;
    bool hasSimt_ = false;
};

class ProfTaskFactory {
public:
    static std::unique_ptr<ProfTask> Create();
};
#endif // __PROF_TASK_H__
