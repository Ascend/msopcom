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


#define private public
#define protected public
#include "runtime/inject_helpers/ProfTask.h"
#undef private
#undef protected

#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#include <map>

#include <gtest/gtest.h>
#include <sys/socket.h>
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/RuntimeOrigin.h"
#include "utils/FileSystem.h"
#include "utils/PipeCall.h"
#include "mockcpp/mockcpp.hpp"
#include "ascend_hal/AscendHalOrigin.h"
#include "core/DomainSocket.h"
#include "runtime/inject_helpers/ProfDataCollect.h"

constexpr uint64_t MEM_ADDR = 0x12c045400000U;
constexpr uint64_t MEM_SIZE = 0x1000U;

/**
/* | 用例集 | ProfTask
/* |测试函数| ProfTaskFactory::Create()
/* | 用例名 | prof_task_factory_create_910B_310P_A5_task_and_expect_success
/* |用例描述| 执行测试函数，返回task指针不为空
*/
TEST(ProfTask, prof_task_factory_create_910B_310P_A5_task_and_expect_success)
{
    GlobalMockObject::verify();
    RuntimeOriginCtor();
    DeviceContext::Local().SetDeviceId(1);
    DeviceContext::Local().SetSocVersion("Ascend910B");
    std::unique_ptr<ProfTask> task1 = ProfTaskFactory::Create();
    ASSERT_TRUE(task1 != nullptr);
    DeviceContext::Local().SetSocVersion("Ascend310P");
    std::unique_ptr<ProfTask> task2 = ProfTaskFactory::Create();
    ASSERT_TRUE(task2 != nullptr);
    DeviceContext::Local().SetSocVersion("Ascend950DT_9591");
    std::unique_ptr<ProfTask> task3 = ProfTaskFactory::Create();
    ASSERT_TRUE(task3 != nullptr);
    GlobalMockObject::verify();
}

TEST(ProfTask, start_prof_task_910B_success)
{
    DeviceContext::Local().SetDeviceId(0);
    DeviceContext::Local().SetSocVersion("Ascend910B");
    std::unique_ptr<ProfTask> task1 = ProfTaskFactory::Create();
    ASSERT_TRUE(task1 != nullptr);
    task1->profTaskConfig_.aicPmu[0] = 1;
    task1->profTaskConfig_.aivPmu[0] = 1;
    task1->profTaskConfig_.l2CachePmu[0] = 0;
    MOCKER(prof_drv_start_origin)
            .expects(exactly(3))
            .will(returnValue(0));
    MOCKER(&KernelContext::GetMC2Flag)
            .stubs()
            .will(returnValue(true));
    ASSERT_TRUE(task1->Start(0, false));
    GlobalMockObject::verify();
}

TEST(ProfTask, start_prof_task_910B_basic_info_success)
{
    DeviceContext::Local().SetDeviceId(0);
    DeviceContext::Local().SetSocVersion("Ascend910B");
    std::unique_ptr<ProfTask> task1 = ProfTaskFactory::Create();
    task1->profTaskConfig_.aicPmu[0] = 0;
    task1->profTaskConfig_.aivPmu[0] = 0;
    ASSERT_TRUE(task1 != nullptr);
    MOCKER(prof_drv_start_origin)
            .expects(exactly(1))
            .will(returnValue(0));
    ASSERT_TRUE(task1->Start(0, false));
    GlobalMockObject::verify();
}

TEST(ProfTask, start_prof_task_910B_ffts_fail)
{
    DeviceContext::Local().SetDeviceId(0);
    DeviceContext::Local().SetSocVersion("Ascend910B");
    std::unique_ptr<ProfTask> task1 = ProfTaskFactory::Create();
    ASSERT_TRUE(task1 != nullptr);
    MOCKER(prof_drv_start_origin)
            .expects(exactly(1))
            .will(returnValue(1));
    ASSERT_FALSE(task1->Start(0, false));
    GlobalMockObject::verify();
}

TEST(ProfTask, start_prof_task_910B_aicpu_fail)
{
    DeviceContext::Local().SetDeviceId(0);
    DeviceContext::Local().SetSocVersion("Ascend910B");
    std::unique_ptr<ProfTask> task1 = ProfTaskFactory::Create();
    ASSERT_TRUE(task1 != nullptr);
    task1->profTaskConfig_.l2CachePmu[0] = 0;
    task1->profTaskConfig_.aicPmu[0] = 1;
    task1->profTaskConfig_.aivPmu[0] = 1;
    MOCKER(prof_drv_start_origin)
            .expects(exactly(2))
            .will(returnValue(0))
            .then(returnValue(1));
    MOCKER(&KernelContext::GetMC2Flag)
            .stubs()
            .will(returnValue(true));
    ASSERT_FALSE(task1->Start(0, false));
    GlobalMockObject::verify();
}

TEST(ProfTask, start_prof_task_910B_stars_fail)
{
    DeviceContext::Local().SetDeviceId(0);
    DeviceContext::Local().SetSocVersion("Ascend910B");
    std::unique_ptr<ProfTask> task1 = ProfTaskFactory::Create();
    ASSERT_TRUE(task1 != nullptr);
    task1->profTaskConfig_.l2CachePmu[0] = 0;
    task1->profTaskConfig_.aicPmu[0] = 1;
    task1->profTaskConfig_.aivPmu[0] = 1;
    MOCKER(prof_drv_start_origin)
            .expects(exactly(3))
            .will(returnValue(0))
            .then(returnValue(0))
            .then(returnValue(1));
    MOCKER(&KernelContext::GetMC2Flag)
            .stubs()
            .will(returnValue(true));
    ASSERT_FALSE(task1->Start(0, false));
    GlobalMockObject::verify();
}

TEST(ProfTask, start_prof_task_310P_success)
{
    DeviceContext::Local().SetDeviceId(1);
    DeviceContext::Local().SetSocVersion("Ascend310P");
    std::unique_ptr<ProfTask> task1 = ProfTaskFactory::Create();
    ASSERT_TRUE(task1 != nullptr);
    MOCKER(prof_drv_start_origin)
            .expects(exactly(1))
            .will(returnValue(0));
    ASSERT_TRUE(task1->Start(1, false));
    GlobalMockObject::verify();
}

TEST(ProfTask, start_prof_task_310P_get_all_task_success)
{
    MessageOfProfConfig profMessage;
    profMessage.replayCount = 0;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 64, 5);
    std::fill(profMessage.l2CachePmu, profMessage.l2CachePmu + 64, 5);
    ProfConfig::Instance().Init(profMessage);
    DeviceContext::Local().SetDeviceId(1);
    DeviceContext::Local().SetSocVersion("Ascend310P");
    std::unique_ptr<ProfTask> task1 = ProfTaskFactory::Create();
    ASSERT_TRUE(task1 != nullptr);
    MOCKER(prof_drv_start_origin)
            .expects(exactly(2))
            .will(returnValue(0));
    ASSERT_TRUE(task1->Start(0, false));
    GlobalMockObject::verify();
}

TEST(ProfTask, start_prof_task_310P_aicore_fail)
{
    DeviceContext::Local().SetDeviceId(1);
    DeviceContext::Local().SetSocVersion("Ascend310P");
    std::unique_ptr<ProfTask> task1 = ProfTaskFactory::Create();
    ASSERT_TRUE(task1 != nullptr);
    MOCKER(prof_drv_start_origin)
            .expects(exactly(1))
            .will(returnValue(1));
    ASSERT_FALSE(task1->Start(1, false));
    GlobalMockObject::verify();
}

TEST(ProfTask, prof_task_channel_read_write_fail)
{
    constexpr int PROF_CHANNEL_NUM = 6;
    using namespace std;
    DeviceContext::Local().SetDeviceId(1);
    DeviceContext::Local().SetSocVersion("Ascend910B");
    std::unique_ptr<ProfTask> task1 = ProfTaskFactory::Create();
    ASSERT_TRUE(task1 != nullptr);
    string path = "./output";
    ASSERT_TRUE(MkdirRecusively(path));
    prof_poll_info_t channels[PROF_CHANNEL_NUM];
    channels[0].device_id = 1;
    channels[0].channel_id = CHANNEL_FFTS_PROFILE_BUFFER_TASK;
    unsigned int channel = 43;
    MOCKER(prof_channel_poll_origin)
            .expects(exactly(1))
            .with(outBoundP(channels), any(), any(), any())
            .will(returnValue(1));
    MOCKER(prof_channel_read_origin)
            .expects(exactly(1))
            .with(any(), outBound(channel), any(), any())
            .will(returnValue(16));
    task1->profRunning_ = false;
    task1->ChannelRead();
    string filePath = JoinPath({path, "DeviceProf1.bin"});
    ASSERT_TRUE(!IsPathExists(filePath));
    RemoveAll(path);
    GlobalMockObject::verify();
}

TEST(ProfTask, prof_task_channel_read_write_success)
{
    constexpr int PROF_CHANNEL_NUM = 6;
    DeviceContext::Local().SetDeviceId(1);
    DeviceContext::Local().SetSocVersion("Ascend910B");
    std::string path = "./output";
    ASSERT_TRUE(MkdirRecusively(path));
    std::unique_ptr<ProfTask> task = ProfTaskFactory::Create();

    prof_poll_info_t channels[PROF_CHANNEL_NUM];
    channels[0].device_id = 1;
    channels[0].channel_id = CHANNEL_FFTS_PROFILE_BUFFER_TASK;
    unsigned int channel = 43;
    MOCKER(prof_channel_poll_origin)
            .expects(exactly(1))
            .with(outBoundP(channels), any(), any(), any())
            .will(returnValue(1));
    MOCKER(prof_channel_read_origin)
            .expects(exactly(1))
            .with(any(), outBound(channel), any(), any())
            .will(returnValue(16));
    MOCKER(ProfDataCollect::GetAicoreOutputPath)
            .expects(exactly(1))
            .will(returnValue(path));
    MOCKER(ProfDataCollect::GetDeviceReplayCount)
            .expects(exactly(1))
            .will(returnValue(1));
    task->profRunning_ = false;
    task->ChannelRead();
    std::string filePath = JoinPath({path, "DeviceProf2.bin"});
    ASSERT_TRUE(IsPathExists(filePath));
    RemoveAll(path);
    GlobalMockObject::verify();
}

/**
/* | 用例集 | ProfTask
/* |测试函数| ProfTaskOfA2::StartStarsTask()
/* | 用例名 | test_prof_task_A2_StartStarsTask_and_expect_return_true
/* |用例描述| 执行测试函数，结果返回true
*/
TEST(ProfTask, test_prof_task_A2_StartStarsTask_and_expect_return_true)
{
    MessageOfProfConfig profMessage;
    profMessage.replayCount = 0;
    ProfConfig::Instance().Init(profMessage);
    DeviceContext::Local().SetDeviceId(1);
    DeviceContext::Local().SetSocVersion("Ascend910B4");
    std::unique_ptr<ProfTask> task = ProfTaskFactory::Create();
    ASSERT_TRUE(task != nullptr);
    task->profTaskConfig_.aicPmu[0] = 0;
    task->profTaskConfig_.aivPmu[0] = 0;
    MOCKER(prof_drv_start_origin)
            .stubs()
            .will(returnValue(0));
    ASSERT_TRUE(task->Start(0, false));
    GlobalMockObject::verify();
}

/**
/* | 用例集 | ProfTask
/* |测试函数| ProfTaskOfA5::StartStarsTask()
/* | 用例名 | test_prof_task_A5_StartStarsTask_and_expect_return_true
/* |用例描述| 执行测试函数，结果返回true
*/
TEST(ProfTask, test_prof_task_A5_StartStarsTask_and_expect_return_true)
{
    MessageOfProfConfig profMessage;
    profMessage.replayCount = 0;
    ProfConfig::Instance().Init(profMessage);
    DeviceContext::Local().SetDeviceId(1);
    DeviceContext::Local().SetSocVersion("Ascend950PR_9589");
    std::unique_ptr<ProfTask> task = ProfTaskFactory::Create();
    ASSERT_TRUE(task != nullptr);
    task->profTaskConfig_.aicPmu[0] = 0;
    task->profTaskConfig_.aivPmu[0] = 0;
    MOCKER(prof_drv_start_origin)
            .stubs()
            .will(returnValue(0));
    ASSERT_TRUE(task->Start(0, false));
    GlobalMockObject::verify();
}

/**
/* | 用例集 | ProfTask
/* |测试函数| ProfTaskOfA5::Start(uint32_t replayCount)
/* | 用例名 | test_prof_task_A5_start_and_expect_return_true
/* |用例描述| 执行测试函数，结果返回true
*/
TEST(ProfTask, test_prof_task_A5_start_and_expect_return_true)
{
    MessageOfProfConfig profMessage;
    profMessage.replayCount = 0;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 64, 5);
    std::fill(profMessage.l2CachePmu, profMessage.l2CachePmu + 64, 5);
    ProfConfig::Instance().Init(profMessage);
    DeviceContext::Local().SetDeviceId(1);
    DeviceContext::Local().SetSocVersion("Ascend950PR_9589");
    std::unique_ptr<ProfTask> task = ProfTaskFactory::Create();
    ProfConfig::Instance().profConfig_.dbiFlag = 0;
    ASSERT_TRUE(task != nullptr);
    MOCKER(prof_drv_start_origin)
            .stubs()
            .will(returnValue(0));
    ASSERT_TRUE(task->Start(0, false));
    GlobalMockObject::verify();
}

/**
/* | 用例集 | ProfTask
/* |测试函数| ProfTaskOfA5::StartInstrProfTask()
/* | 用例名 | test_A5_start_instr_task_when_pipe_timeline_enable_then_return_true
/* |用例描述| 执行测试函数，pipe timeline使能情况下返回true
*/
TEST(ProfTask, test_A5_start_instr_task_when_pipe_timeline_enable_then_return_true)
{
    MessageOfProfConfig profMessage;
    profMessage.replayCount = 0;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 64, 5);
    std::fill(profMessage.l2CachePmu, profMessage.l2CachePmu + 64, 5);
    ProfConfig::Instance().Init(profMessage);
    DeviceContext::Local().SetDeviceId(1);
    DeviceContext::Local().SetSocVersion("Ascend950PR_9589");
    std::unique_ptr<ProfTask> task = ProfTaskFactory::Create();
    ASSERT_TRUE(task != nullptr);
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_END;
    MOCKER(prof_drv_start_origin)
            .stubs()
            .will(returnValue(0));
    ASSERT_TRUE(task->Start(0, false));
    GlobalMockObject::verify();
}

/**
/* | 用例集 | ProfTask
/* |测试函数| ProfTaskOfA5::StartInstrProfTask()
/* | 用例名 | test_A5_start_instr_task_when_pcSampling_enable_then_return_true
/* |用例描述| 执行测试函数，pcSampling使能情况下返回true
*/
TEST(ProfTask, test_A5_start_instr_task_when_pcSampling_enable_then_return_true)
{
    MessageOfProfConfig profMessage;
    profMessage.replayCount = 0;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 64, 5);
    std::fill(profMessage.l2CachePmu, profMessage.l2CachePmu + 64, 5);
    ProfConfig::Instance().Init(profMessage);
    DeviceContext::Local().SetDeviceId(1);
    DeviceContext::Local().SetSocVersion("Ascend950PR_9589");
    std::unique_ptr<ProfTask> task = ProfTaskFactory::Create();
    ASSERT_TRUE(task != nullptr);
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_START;
    MOCKER(prof_drv_start_origin)
            .stubs()
            .will(returnValue(0));
    ASSERT_TRUE(task->Start(0, false));
    GlobalMockObject::verify();
}

/**
/* | 用例集 | ProfTask
/* |测试函数| ProfTaskOfA5::StartInstrProfTask()
/* | 用例名 | test_A5_start_instr_task_when_instr_timeline_enable_then_return_true
/* |用例描述| 执行测试函数，instr timeline使能情况下返回true
*/
TEST(ProfTask, test_A5_start_instr_task_when_instr_timeline_enable_then_return_true)
{
    MessageOfProfConfig profMessage;
    profMessage.replayCount = 0;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 64, 5);
    ProfConfig::Instance().Init(profMessage);
    DeviceContext::Local().SetDeviceId(1);
    DeviceContext::Local().SetSocVersion("Ascend950PR_9589");
    std::unique_ptr<ProfTask> task = ProfTaskFactory::Create();
    ASSERT_TRUE(task != nullptr);
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_DFX;
    MOCKER(prof_drv_start_origin)
            .stubs()
            .will(returnValue(0));
    ASSERT_TRUE(task->Start(0, false));
    GlobalMockObject::verify();
}

/**
/* | 用例集 | ProfTask
/* |测试函数| ProfTask::ChannelRead()
/* | 用例名 | test_A5_channel_read_when_timeline_or_pcsampling_enabled_and_expect_success
/* |用例描述| 执行测试函数，启用 timeline 或者 pc sampling，生成文件成功
*/
TEST(ProfTask, test_A5_channel_read_when_timeline_or_pcsampling_enabled_and_expect_success)
{
    constexpr int PROF_CHANNEL_NUM = 18;
    using namespace std;
    string path = "./output";
    std::map<int32_t, std::string> aicoreOutputPathMap = {{0, "./"}};
    DeviceContext::Local().SetDeviceId(1);
    DeviceContext::Local().SetSocVersion("Ascend950PR_9589");
    prof_poll_info_t channels[PROF_CHANNEL_NUM];
    channels[0].device_id = 1;
    channels[0].channel_id = static_cast<uint32_t>(InstrChannel::GROUP0_AIC);
    MOCKER(prof_drv_start_origin)
        .stubs()
        .will(returnValue(0));
    MOCKER(prof_stop_origin)
        .stubs()
        .will(returnValue(0));
    MOCKER(prof_channel_poll_origin)
        .stubs()
        .with(outBoundP(channels), any(), any(), any())
        .will(returnValue(1));
    MOCKER(prof_channel_read_origin)
        .stubs()
        .will(returnValue(16));
    MOCKER(ProfDataCollect::GetAicoreOutputPath)
        .stubs()
        .will(returnValue(path));
    ASSERT_TRUE(MkdirRecusively(path));

    // 测试 timeline 采集流程: IDLE → TIMELINE → DONE
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_END;
    std::unique_ptr<ProfTask> task1 = ProfTaskFactory::Create();
    ASSERT_TRUE(task1 != nullptr);
    task1->Start(0, false);
    task1->profRunning_ = false;
    task1->ChannelRead();
    string filePath = JoinPath({path, "timeline.bin.0"});
    task1->Stop();
    ASSERT_TRUE(IsPathExists(filePath));

    // 测试 pcSampling 采集流程: IDLE → PC_SAMPLING → DONE
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_START;
    std::unique_ptr<ProfTask> task2 = ProfTaskFactory::Create();
    ASSERT_TRUE(task2 != nullptr);
    task2->Start(0, true);
    task2->profRunning_ = false;
    task2->ChannelRead();
    filePath = JoinPath({path, "pcSampling.bin.0"});
    task2->Stop();
    ASSERT_TRUE(IsPathExists(filePath));

    RemoveAll(path);
    GlobalMockObject::verify();
}
