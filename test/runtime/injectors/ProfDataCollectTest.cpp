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
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "runtime/inject_helpers/ProfConfig.h"
#undef private

#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <gtest/gtest.h>
#include "runtime/inject_helpers/BBCountDumper.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/RegisterManager.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/MemoryContext.h"
#include "runtime/inject_helpers/ProfTask.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"

#include "runtime/RuntimeOrigin.h"
#include "ascendcl/AscendclOrigin.h"
#include "acl_rt_impl//AscendclImplOrigin.h"
#include "hccl/HcclOrigin.h"
#include "utils/FileSystem.h"
#include "utils/PipeCall.h"
#include "mockcpp/mockcpp.hpp"
#include "ascend_hal/AscendHalOrigin.h"
#include "core/DomainSocket.h"
#include "utils/ElfLoader.h"

constexpr uint64_t MEM_ADDR = 0x12c045400000U;
constexpr uint64_t MEM_SIZE = 0x1000U;
static bool GetValidNameFromBinary(const char *data,
                                   uint64_t length,
                                   std::vector<std::string> &kernelNames,
                                   std::vector<uint64_t> &kernelOffsets)
{
    kernelNames.emplace_back("valid_kernel_1234_mix_aic");
    kernelOffsets.emplace_back(0);
    return true;
}

class ProfDataCollectTest : public testing::Test {
public:
    static void SetUpTestCase() {}

    void SetUp() override
    {
        path_ = "./tmp";
    }
    void TearDown() override
    {
        RemoveAll(path_);
        GlobalMockObject::verify();
    }
    std::string path_;
};

TEST_F(ProfDataCollectTest, prof_success_when_app_replay)
{
    MOCKER(&setsockopt).stubs().will(returnValue(-1));
    MOCKER(&connect).stubs().will(returnValue(-1));
    MOCKER(&socket).stubs().will(returnValue(-1));
    MOCKER(&write).stubs().will(returnValue(-1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend310P");
    MessageOfProfConfig profMessage;
    profMessage.replayCount = 0;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);
    MOCKER(rtProfSetProSwitchOrigin)
           .stubs().will(returnValue(RT_ERROR_NONE));
    MOCKER(aclrtSynchronizeStreamImplOrigin)
            .stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(prof_channel_poll_origin)
            .stubs().will(returnValue(0));
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    uint8_t memInfo[2] = {1, 2};
    ProfDataCollect profDataCollect;
    ASSERT_TRUE(profDataCollect.ProfData(stream, func));
}

TEST_F(ProfDataCollectTest, prof_310_failed_when_app_replay)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend310P");
    MessageOfProfConfig profMessage;
    profMessage.replayCount = 0;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);
    std::string path = "./aaa";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(rtProfSetProSwitchOrigin)
            .stubs().will(returnValue(RT_ERROR_NONE));
    MOCKER(&MemoryContext::Backup).stubs().will(returnValue(true));
    MOCKER(&MemoryContext::Restore).stubs().will(returnValue(true));
    MOCKER(aclrtSynchronizeStreamImplOrigin)
            .stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(prof_drv_start_origin)
            .stubs().will(returnValue(-1));
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    uint8_t memInfo[2] = {1, 2};
    ProfDataCollect profDataCollect;
    ASSERT_FALSE(profDataCollect.ProfData(stream, func));
    RemoveAll(path);
}

TEST_F(ProfDataCollectTest, prof_data_and_exec_simulator_dump_failed)
{
    MOCKER(&setsockopt).stubs().will(returnValue(-1));
    MOCKER(&connect).stubs().will(returnValue(-1));
    MOCKER(&socket).stubs().will(returnValue(-1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(-1));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend310P");
    MessageOfProfConfig profMessage;
    profMessage.replayCount = 0;
    profMessage.isDeviceToSimulator = true;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);
    std::string path = "./aaa";
    std::string tmpPath = "./aaa/tmp";
    MOCKER(&GetEnv).stubs().will(returnValue(tmpPath));
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(&MemoryContext::Backup).stubs().will(returnValue(true));
    MOCKER(&MemoryContext::Restore).stubs().will(returnValue(true));
    bool(KernelDumper::*funcPtr)(const std::string &, uint64_t, bool) const = &KernelDumper::Dump;
    MOCKER(funcPtr).stubs().will(returnValue(false));

    MOCKER(aclrtSynchronizeStreamWithTimeoutImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(prof_drv_start_origin).stubs().will(returnValue(0));
    MOCKER(prof_channel_poll_origin)
            .stubs().will(returnValue(0));
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    uint8_t memInfo[2] = {1, 2};
    ProfDataCollect profDataCollect;
    ASSERT_TRUE(profDataCollect.ProfData(stream, func));
}

TEST_F(ProfDataCollectTest, prof_data_and_exec_simulator_dump_succ)
{
    MOCKER(&setsockopt).stubs().will(returnValue(-1));
    MOCKER(&connect).stubs().will(returnValue(-1));
    MOCKER(&socket).stubs().will(returnValue(-1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(-1));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend310P");
    MessageOfProfConfig profMessage;
    profMessage.replayCount = 0;
    profMessage.isDeviceToSimulator = true;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);
    std::string path = "./OPPO2024/device0/Add/0";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(&MemoryContext::Backup).stubs().will(returnValue(true));
    MOCKER(&MemoryContext::Restore).stubs().will(returnValue(true));
    bool(KernelDumper::*funcPtr)(const std::string &, uint64_t, bool) const = &KernelDumper::Dump;
    MOCKER(funcPtr).stubs().will(returnValue(true));

    MOCKER(aclrtSynchronizeStreamWithTimeoutImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(prof_drv_start_origin).stubs().will(returnValue(0));
    MOCKER(prof_channel_poll_origin)
            .stubs().will(returnValue(0));
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    uint8_t memInfo[2] = {1, 2};
    ProfDataCollect profDataCollect;
    ASSERT_TRUE(profDataCollect.ProfData(stream, func));
}

TEST_F(ProfDataCollectTest, prof_success_when_kernel_replay_start_success)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend310P");
    MessageOfProfConfig profMessage;
    profMessage.replayCount = UINT32_INVALID;
    profMessage.profWarmUpTimes = 5;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);
    std::string path = "./aaa";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(&MemoryContext::Backup).stubs().will(returnValue(true));
    MOCKER(&MemoryContext::Restore).stubs().will(returnValue(true));

    MOCKER(aclrtSynchronizeStreamWithTimeoutImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(prof_drv_start_origin).stubs().will(returnValue(0));
    MOCKER(prof_channel_poll_origin)
            .stubs().will(returnValue(0));
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    uint8_t memInfo[2] = {1, 2};
    ProfDataCollect profDataCollect;
    ASSERT_TRUE(profDataCollect.ProfData(stream, func));
}

TEST_F(ProfDataCollectTest, prof_success_when_kernel_replay_start_success_and_save_success)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend310P");
    MessageOfProfConfig profMessage;
    profMessage.replayCount = UINT32_INVALID;
    profMessage.profWarmUpTimes = 5;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);
    std::string path = "./aaa";
    MkdirRecusively(path);
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(&MemoryContext::Backup).stubs().will(returnValue(true));
    MOCKER(&MemoryContext::Restore).stubs().will(returnValue(true));

    MOCKER(aclrtSynchronizeStreamWithTimeoutImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(prof_drv_start_origin).stubs().will(returnValue(0));
    MOCKER(prof_channel_poll_origin)
            .stubs().will(returnValue(0));
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    uint8_t memInfo[2] = {1, 2};
    ProfDataCollect profDataCollect;
    ASSERT_TRUE(profDataCollect.ProfData(stream, func));
    RemoveAll(path);
}

TEST_F(ProfDataCollectTest, ProfData_success_when_mc2_operator_run_success)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend910B");
    MessageOfProfConfig profMessage;
    profMessage.replayCount = UINT32_INVALID;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);
    std::string path = "./tmp";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(&KernelContext::GetMC2Flag).stubs().will(returnValue(true));
    MOCKER(prof_drv_start_origin).stubs().will(returnValue(0));
    MOCKER(prof_channel_poll_origin).stubs().will(returnValue(0));

    KernelContext::Instance().SetHcclComm(&path);
    MOCKER(aclrtCreateEventOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(aclrtRecordEventOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(aclrtStreamWaitEventOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(aclrtResetEventOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(HcclBarrierOrigin).stubs().will(returnValue(HCCL_SUCCESS));
    MOCKER(rtAicpuKernelLaunchExWithArgsOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    MOCKER(aclrtSynchronizeStreamWithTimeoutImplOrigin).stubs().will(returnValue(ACL_SUCCESS));

    rtStream_t stream;
    auto func = []() {
        return true;
    };
    ProfDataCollect profDataCollect;
    ASSERT_TRUE(profDataCollect.ProfData(stream, func));
}

TEST_F(ProfDataCollectTest, ProfData_failed_when_mc2_operator_get_hccl_comm_failed)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend910B");
    MessageOfProfConfig profMessage;
    profMessage.replayCount = UINT32_INVALID;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);
    std::string path = "./tmp";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(&KernelContext::GetMC2Flag).stubs().will(returnValue(true));
    MOCKER(prof_drv_start_origin).stubs().will(returnValue(0));
    MOCKER(prof_channel_poll_origin).stubs().will(returnValue(0));
    KernelContext::Instance().SetHcclComm(nullptr);

    rtStream_t stream;
    auto func = []() {
        return true;
    };
    ProfDataCollect profDataCollect;
    ASSERT_FALSE(profDataCollect.ProfData(stream, func));
}

TEST_F(ProfDataCollectTest, get_bb_count_success)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend910B");
    MessageOfProfConfig profMessage;
    profMessage.replayCount = UINT32_INVALID;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);
    std::string path = "./aaa";
    MkdirRecusively(path);
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(&MemoryContext::Backup).stubs().will(returnValue(true));
    MOCKER(&MemoryContext::Restore).stubs().will(returnValue(true));

    MOCKER(aclrtSynchronizeStreamWithTimeoutImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(prof_drv_start_origin).stubs().will(returnValue(0));
    MOCKER(prof_channel_poll_origin)
            .stubs().will(returnValue(0));
    std::string dumpPath = "./bbb";
    MkdirRecusively(dumpPath);
    MOCKER(&GetEnv).stubs().will(returnValue(dumpPath));
    std::string text = "kernel1.extra.0";
    MOCKER(&BBCountDumper::GenExtraAndReturnName).stubs().will(returnValue(text));
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    uint8_t memInfo[2] = {1, 2};
    ProfDataCollect profDataCollect;
    ASSERT_TRUE(profDataCollect.ProfData(stream, func));

    DeviceContext::Local().SetSocVersion("Ascend310P");
    ASSERT_TRUE(profDataCollect.ProfData(stream, func));

    DeviceContext::Local().SetSocVersion("Ascend910B");
    auto ret = WriteStringToFile(JoinPath({dumpPath, "kernel1Stub.o.bbbmap.0"}), "1");
    ASSERT_TRUE(profDataCollect.ProfData(stream, func));

    WriteStringToFile(JoinPath({dumpPath, "kernel1.o"}), "1");
    profDataCollect.GenBBcountFile(1, 2, memInfo);
    RemoveAll(path);
    RemoveAll(dumpPath);
}

TEST_F(ProfDataCollectTest, get_bb_count_failed)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));

    uint8_t memInfo[2] = {1, 2};
    std::string path;
    std::string dumpPath = "./bbb";
    MkdirRecusively(dumpPath);
    MOCKER(&GetEnv).stubs().will(returnValue(path)).then(returnValue(dumpPath));
    ProfDataCollect profDataCollect;
    profDataCollect.GenBBcountFile(1, 2, memInfo);
    std::string text;
    MOCKER(&BBCountDumper::GenExtraAndReturnName).stubs().will(returnValue(text));

    WriteStringToFile(JoinPath({dumpPath, "kernel1.o"}), "1");
    profDataCollect.GenBBcountFile(1, 2, memInfo);
    RemoveAll(path);
    RemoveAll(dumpPath);
}

TEST_F(ProfDataCollectTest, prof_failed_when_kernel_replay_restore_failed)
{
    MOCKER(&setsockopt).stubs().will(returnValue(-1));
    MOCKER(&connect).stubs().will(returnValue(-1));
    MOCKER(&socket).stubs().will(returnValue(-1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend310P");
    MessageOfProfConfig profMessage;
    profMessage.replayCount = UINT32_INVALID;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    std::string path = "./aaa";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(&MemoryContext::Backup).stubs().will(returnValue(true));
    MOCKER(&MemoryContext::Restore).stubs().will(returnValue(false));
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    ProfDataCollect profDataCollect;
    ASSERT_FALSE(profDataCollect.ProfData(stream, func));
    RemoveAll(path);
}


TEST_F(ProfDataCollectTest, prof_failed_when_kernel_replay_start_failed)
{
    MOCKER(&setsockopt).stubs().will(returnValue(-1));
    MOCKER(&connect).stubs().will(returnValue(-1));
    MOCKER(&socket).stubs().will(returnValue(-1));
    MOCKER(&write).stubs().will(returnValue(-1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend310P");
    MessageOfProfConfig profMessage;
    profMessage.replayCount = UINT32_INVALID;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);
    std::string path = "/aaa";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(&MemoryContext::Backup).stubs().will(returnValue(true));
    MOCKER(&MemoryContext::Restore).stubs().will(returnValue(true));
    MOCKER(prof_drv_start_origin).stubs().will(returnValue(1));
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    uint8_t memInfo[2] = {1, 2};
    ProfDataCollect profDataCollect;
    ASSERT_FALSE(profDataCollect.ProfData(stream, func));
}

TEST_F(ProfDataCollectTest, save_object_success)
{
    using namespace std;
    DeviceContext::Local().SetSocVersion("Ascend310P");
    bool(KernelContext::*funcPtr)(const KernelHandle*, const string&, const string&) = &KernelContext::DumpKernelObject;
    MOCKER(funcPtr).stubs().will(returnValue(true));
    ASSERT_TRUE(ProfDataCollect::SaveObject(nullptr));
}

TEST_F(ProfDataCollectTest, save_object_failed)
{
    using namespace std;
    DeviceContext::Local().SetSocVersion("Ascend910B");
    bool(KernelContext::*funcPtr)(const KernelHandle*, const string&, const string&) = &KernelContext::DumpKernelObject;
    MOCKER(funcPtr).stubs().will(returnValue(false));
    int a = 3;
    ASSERT_FALSE(ProfDataCollect::SaveObject((void *)&a));
}

TEST_F(ProfDataCollectTest, save_object_success_ctx)
{
    using namespace std;
    RegisterContextSP p;
    const char *path = "empty.o";
    uint32_t data{};
    vector<char> elfData(100, 1);
    aclrtBinHandle binHandle = &data;
    MOCKER(&IsExist)
            .stubs()
            .will(returnValue(false))
            .then(returnValue(true));
    MOCKER(&GetSymInfoFromBinary).stubs().will(invoke(GetValidNameFromBinary));
    MOCKER(&HasStaticStub).stubs().will(returnValue(true));
    MOCKER(&RegisterContext::Save).stubs().will(returnValue(true));
    MOCKER_CPP(&ReadBinary).stubs().with(any(), outBound(elfData)).will(returnValue(size_t(1)));
    auto regCtx_ = RegisterManager::Instance().CreateContext(path, binHandle, RT_DEV_BINARY_MAGIC_ELF);
    ASSERT_TRUE(regCtx_ != nullptr);
    ASSERT_TRUE(ProfDataCollect::SaveObject(nullptr));
    ASSERT_TRUE(ProfDataCollect::SaveObject(nullptr));
}

TEST_F(ProfDataCollectTest, test_is_bbcount_need_gen_false)
{
    ProfDataCollect p;
    ProfConfig::Instance().profConfig_.dbiFlag = 0;
    ASSERT_FALSE(p.IsBBCountNeedGen());
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_BB_COUNT;
    MOCKER(&ProfDataCollect::IsNeedProf).stubs().will(returnValue(false));
    ASSERT_FALSE(p.IsMemoryChartNeedGen());
    MOCKER(&KernelContext::SetMC2Flag).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(true));
    ASSERT_FALSE(p.IsBBCountNeedGen());
    MOCKER(&KernelContext::SetMC2Flag).stubs().will(returnValue(false));
    MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(true));
    ASSERT_FALSE(p.IsBBCountNeedGen());
    ProfConfig::Instance().Reset();
}

TEST_F(ProfDataCollectTest, test_is_bbcount_need_gen_true)
{
    ProfDataCollect p;
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_BB_COUNT;
    MOCKER(&ProfDataCollect::IsNeedProf).stubs().will(returnValue(true));
    MOCKER(&KernelContext::SetMC2Flag).stubs().will(returnValue(false));
    MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(false));
    ProfConfig::Instance().isAppReplay_ = false;
    ASSERT_TRUE(p.IsBBCountNeedGen());
    ProfConfig::Instance().isAppReplay_ = true;
    ProfConfig::Instance().Reset();
}

TEST_F(ProfDataCollectTest, test_is_OperandRecord_need_gen_false)
{
    ProfDataCollect p;
    MOCKER(&ProfDataCollect::IsNeedProf).stubs().will(returnValue(false));
    ASSERT_FALSE(p.IsOperandRecordNeedGen());
    MOCKER(&KernelContext::GetMC2Flag).stubs().will(returnValue(false));
    MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(true));
    ASSERT_FALSE(p.IsOperandRecordNeedGen());
    ProfConfig::Instance().Reset();
}

TEST_F(ProfDataCollectTest, test_is_OperandRecord_need_gen_true)
{
    ProfDataCollect p;
    MOCKER(&ProfDataCollect::IsNeedProf).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetMC2Flag).stubs().will(returnValue(false));
    MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(false));
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_OPERAND_RECORD;
    ProfConfig::Instance().isAppReplay_ = false;
    ASSERT_TRUE(p.IsOperandRecordNeedGen());
    ProfConfig::Instance().isAppReplay_ = true;
    ProfConfig::Instance().Reset();
}

TEST_F(ProfDataCollectTest, test_is_memory_chart_need_gen_false)
{
    ProfDataCollect p;
    ProfConfig::Instance().profConfig_.dbiFlag = 0;
    ASSERT_FALSE(p.IsMemoryChartNeedGen());
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_MEMORY_CHART;
    MOCKER(&ProfDataCollect::IsNeedProf).stubs().will(returnValue(false));
    ASSERT_FALSE(p.IsMemoryChartNeedGen());
    MOCKER(&KernelContext::SetMC2Flag).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(true));
    ASSERT_FALSE(p.IsMemoryChartNeedGen());
    MOCKER(&KernelContext::SetMC2Flag).stubs().will(returnValue(false));
    MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(true));
    ASSERT_FALSE(p.IsMemoryChartNeedGen());
    ProfConfig::Instance().Reset();
}

TEST_F(ProfDataCollectTest, test_is_memory_chart_need_gen_true)
{
    ProfDataCollect p;
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_MEMORY_CHART;
    MOCKER(&ProfDataCollect::IsNeedProf).stubs().will(returnValue(true));
    MOCKER(&KernelContext::SetMC2Flag).stubs().will(returnValue(false));
    MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(false));
    ProfConfig::Instance().isAppReplay_ = false;
    ASSERT_TRUE(p.IsMemoryChartNeedGen());
    ProfConfig::Instance().isAppReplay_ = true;
    ProfConfig::Instance().Reset();
}

/**
/* | 用例集 | ProfDataCollectTest
/* |测试函数| ProfDataCollect::IsPCSamplingNeedGen()
/* | 用例名 | test_is_pcsampling_need_gen_false
/* |用例描述| pcsampling未使能或不需要prof时返回false
*/
TEST_F(ProfDataCollectTest, test_is_pcsampling_need_gen_false)
{
    ProfDataCollect p;
    ProfConfig::Instance().profConfig_.dbiFlag = 0;
    ASSERT_FALSE(p.IsPCSamplingNeedGen());
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_START;
    MOCKER(&ProfDataCollect::IsNeedProf).stubs().will(returnValue(false));
    ASSERT_FALSE(p.IsPCSamplingNeedGen());
    ProfConfig::Instance().Reset();
}

/**
/* | 用例集 | ProfDataCollectTest
/* |测试函数| ProfDataCollect::IsPCSamplingNeedGen()
/* | 用例名 | test_is_pcsampling_need_gen_true
/* |用例描述| pcsampling使能且需要prof时返回true
*/
TEST_F(ProfDataCollectTest, test_is_pcsampling_need_gen_true)
{
    ProfDataCollect p;
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_START;
    MOCKER(&ProfDataCollect::IsNeedProf).stubs().will(returnValue(true));
    ASSERT_TRUE(p.IsPCSamplingNeedGen());
    ProfConfig::Instance().Reset();
}

/**
 * | 用例集 | ProfDataCollectTest
 * |测试函数| ProfDataCollect::IsTimelineNeedGen()
 * | 用例名 | test_is_timeline_need_gen_false
 * |用例描述| timeline未使能或不需要prof时返回false
 */
TEST_F(ProfDataCollectTest, test_is_timeline_need_gen_false)
{
    ProfDataCollect p;
    ProfDBIType type;
    ProfConfig::Instance().profConfig_.dbiFlag = 0;
    ASSERT_FALSE(p.IsTimelineNeedGen(type));
    MOCKER(&ProfDataCollect::IsNeedProf).stubs().will(returnValue(false));
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_END;
    ASSERT_FALSE(p.IsTimelineNeedGen(type));
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_DFX;
    ASSERT_FALSE(p.IsTimelineNeedGen(type));
    ProfConfig::Instance().Reset();
}

/**
 * | 用例集 | ProfDataCollectTest
 * |测试函数| ProfDataCollect::IsTimelineNeedGen()
 * | 用例名 | test_is_timeline_need_gen_pipe_timeline_true
 * |用例描述| pipetimeline使能且需要prof时返回true并设置正确类型
 */
TEST_F(ProfDataCollectTest, test_is_timeline_need_gen_pipe_timeline_true)
{
    ProfDataCollect p;
    ProfDBIType type;
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_END;
    MOCKER(&ProfDataCollect::IsNeedProf).stubs().will(returnValue(true));
    ASSERT_TRUE(p.IsTimelineNeedGen(type));
    ASSERT_EQ(type, ProfDBIType::INSTR_PROF_END);
    ProfConfig::Instance().Reset();
}

/**
 * | 用例集 | ProfDataCollectTest
 * |测试函数| ProfDataCollect::IsTimelineNeedGen()
 * | 用例名 | test_is_timeline_need_gen_instr_timeline_true
 * |用例描述| instrtimeline使能且需要prof时返回true并设置正确类型
 */
TEST_F(ProfDataCollectTest, test_is_timeline_need_gen_instr_timeline_true)
{
    ProfDataCollect p;
    ProfDBIType type;
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_DFX;
    MOCKER(&ProfDataCollect::IsNeedProf).stubs().will(returnValue(true));
    ASSERT_TRUE(p.IsTimelineNeedGen(type));
    ASSERT_EQ(type, ProfDBIType::INSTR_PROF_DFX);
    ProfConfig::Instance().Reset();
}

/**
 * | 用例集 | ProfDataCollectTest
 * |测试函数| ProfDataCollect::IsTimelineNeedGen()
 * | 用例名 | test_is_timeline_need_gen_instr_priority
 * |用例描述| instrtimeline优先级高于pipetimeline
 */
TEST_F(ProfDataCollectTest, test_is_timeline_need_gen_instr_priority)
{
    ProfDataCollect p;
    ProfDBIType type;
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_DFX | DBI_FLAG_INSTR_PROF_END;
    MOCKER(&ProfDataCollect::IsNeedProf).stubs().will(returnValue(true));
    ASSERT_TRUE(p.IsTimelineNeedGen(type));
    ASSERT_EQ(type, ProfDBIType::INSTR_PROF_DFX);
    ProfConfig::Instance().Reset();
}

TEST_F(ProfDataCollectTest, test_need_run_origin_when_warp_timeline_has_no_simt) {
    ProfConfig::Instance().Reset();
    ProfConfig::Instance().isAppReplay_ = true;
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_WARP_TIMELINE;
    std::string path = "./tmp/warp_timeline_no_simt";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(&KernelContext::HasSimtSymbol).stubs().will(returnValue(false));
    MOCKER(&KernelContext::GetMC2Flag).stubs().will(returnValue(false));
    MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(false));

    ProfDataCollect profDataCollect;
    ASSERT_FALSE(profDataCollect.IsWarpTimelineNeedGen());
    ASSERT_TRUE(profDataCollect.IsNeedRunOriginLaunch());
    ProfConfig::Instance().Reset();
}

TEST_F(ProfDataCollectTest, prof_success_but_l2cache_init_failed)
{
    ProfConfig::Instance().isAppReplay_ = false;
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend310P1");
    MessageOfProfConfig profMessage;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);
    std::string path = "./aaa";
    MkdirRecusively(path);
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(&MemoryContext::Backup).stubs().will(returnValue(true));
    MOCKER(&MemoryContext::Restore).stubs().will(returnValue(true));

    MOCKER(aclrtSynchronizeStreamWithTimeoutImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(prof_drv_start_origin).stubs().will(returnValue(0));
    MOCKER(prof_channel_poll_origin)
            .stubs().will(returnValue(0));
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    uint8_t memInfo[2] = {1, 2};

    ProfDataCollect profDataCollect;
    ASSERT_TRUE(profDataCollect.ProfData(stream, func));
    RemoveAll(path);
}

TEST_F(ProfDataCollectTest, prof_data_output_is_null)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend910B");
    std::string path = "./tmp";
    MkdirRecusively(path);
    MOCKER(IsDir).stubs().will(returnValue(true));
    MOCKER(GetEnv).stubs().will(returnValue(path));
    MOCKER(IsExist).stubs().will(returnValue(true));
    std::string outPath;
    ProfConfig::Instance().profConfig_.isSimulator = true;
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(outPath));
    MkdirRecusively(JoinPath({path, "Add_1"}));
    ProfDataCollect profDataCollect;
    profDataCollect.ProfInit(nullptr);
    ASSERT_TRUE(profDataCollect.ProfData());
    RemoveAll(path);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    ProfConfig::Instance().Reset();
}

TEST_F(ProfDataCollectTest, prof_data_output_is_not_null)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend910B");
    std::string path = "./tmp";
    std::string outPath = "./tmp/aa";
    MkdirRecusively(path);
    MkdirRecusively(outPath);
    MOCKER(IsDir).stubs().will(returnValue(true));
    MOCKER(GetEnv).stubs().will(returnValue(path));
    MOCKER(IsExist).stubs().will(returnValue(true));
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(outPath));
    ProfConfig::Instance().profConfig_.isSimulator = true;
    ProfDataCollect profDataCollect;
    profDataCollect.ProfInit(nullptr);
    ASSERT_TRUE(profDataCollect.ProfData());
    RemoveAll(path);
    ProfConfig::Instance().profConfig_.isSimulator = false;
}

TEST_F(ProfDataCollectTest, prof_data_tmp_is_error)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");
    ProfConfig::Instance().profConfig_.isSimulator = true;
    ProfDataCollect profDataCollect;
    profDataCollect.ProfInit(nullptr);
    EXPECT_FALSE(profDataCollect.ProfData());
    std::string path = "./tmp";
    MOCKER(GetEnv).stubs().will(returnValue(path));
    ProfDataCollect profDataCollect1;
    profDataCollect1.ProfInit(nullptr);
    EXPECT_FALSE(profDataCollect1.ProfData());
    ProfConfig::Instance().profConfig_.isSimulator = false;
}

TEST_F(ProfDataCollectTest, clear_ca_file)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");
    ProfConfig::Instance().profConfig_.isSimulator = true;
    std::string path = "./tmp";
    std::string tmpPath = JoinPath({path, "Add_1"});
    MkdirRecusively(tmpPath);
    std::string s = "aaaaa";
    WriteBinary(JoinPath({tmpPath, "a.txt"}), s.c_str(), 5);
    MOCKER(GetEnv).stubs().will(returnValue(tmpPath));
    MOCKER(IsDir).stubs().will(returnValue(false));
    MOCKER(IsSoftLink).stubs().will(returnValue(false));
    ProfDataCollect profDataCollect;
    profDataCollect.ProfInit(nullptr);
    ASSERT_TRUE(profDataCollect.ProfData());
    RemoveAll(path);
    ProfConfig::Instance().profConfig_.isSimulator = false;
}

TEST_F(ProfDataCollectTest, do_post_process)
{
    ProfDataCollect profDataCollect;

    MessageOfProfConfig profConfig;
    ProfConfig::Instance().Init(profConfig);
    MOCKER(&ProfConfig::PostNotify).stubs().will(returnValue(true));
    profDataCollect.PostProcess();
}

TEST_F(ProfDataCollectTest, gen_dbi_data_failed)
{
    MOCKER(aclrtFreeImplOrigin)
        .stubs()
        .will(returnValue(ACL_ERROR_BAD_ALLOC));
    ProfDataCollect profDataCollect;
    uint64_t memSize = 0;
    uint8_t *memInfo = nullptr;
    profDataCollect.GenDBIData(memSize, memInfo);

    MOCKER(aclrtMemcpyImplOrigin)
        .expects(exactly(2))
        .will(returnValue(ACL_ERROR_BAD_ALLOC)).then(returnValue(ACL_SUCCESS));
    profDataCollect.GenDBIData(BLOCK_MEM_SIZE, memInfo);

    MOCKER(&LocalProcess::Notify)
        .stubs()
        .will(returnValue(0UL));
    profDataCollect.GenDBIData(BLOCK_MEM_SIZE, memInfo);
}

TEST_F(ProfDataCollectTest, prof_init_common_operator)
{
    std::string path = "./tmp";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    ProfDataCollect profDataCollect;
    profDataCollect.ProfInit(nullptr);
    ASSERT_FALSE(KernelContext::Instance().GetMC2Flag());
}

TEST_F(ProfDataCollectTest, prof_init_mc2_operator)
{
    std::string path = "./tmp";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(&KernelContext::GetMC2Flag).stubs().will(returnValue(true));
    ProfDataCollect profDataCollect;
    profDataCollect.ProfInit(nullptr);
    ASSERT_FALSE(IsPathExists(path + "/aicore_binary.o"));
}

TEST_F(ProfDataCollectTest, TerminateInAdvance)
{
    MOCKER(aclrtGetDeviceImplOrigin).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER(&LocalProcess::TerminateWithSignal).stubs().will(returnValue(0));
    ProfDataCollect profDataCollect;
    profDataCollect.TerminateInAdvance();
}

TEST_F(ProfDataCollectTest, GetAicoreOutputPath_expect_return_empty_string)
{
    EXPECT_TRUE(ProfDataCollect::GetAicoreOutputPath(INT32_MAX).empty());
}

TEST_F(ProfDataCollectTest, GetDeviceReplayCount_expect_return_zero)
{
    EXPECT_EQ(ProfDataCollect::GetDeviceReplayCount(INT32_MAX), 0);
}

TEST_F(ProfDataCollectTest, GetThreadRangeConfigMap_expect_return_default_value)
{
    auto res = ProfDataCollect::GetThreadRangeConfigMap(std::this_thread::get_id());
    EXPECT_EQ(res.flag, false);
    EXPECT_EQ(res.count, 0);
    EXPECT_TRUE(res.outputs.empty());
}

TEST_F(ProfDataCollectTest, ProfInit_set_lccl_operator_failed)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend910B");
    std::string path = "./tmp";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    using GetDevBinaryFunc = bool(KernelContext::*)(KernelContext::KernelHandlePtr, rtDevBinary_t &, bool) const;
    GetDevBinaryFunc getDevBinary = &KernelContext::GetDevBinary;
    MOCKER(getDevBinary).stubs().will(returnValue(true));
    using GetSectionHeadersFunc = bool(*)(rtDevBinary_t const &, std::map<std::string, Elf64_Shdr> &);
    GetSectionHeadersFunc getSectionHeaders = &GetSectionHeaders;
    MOCKER(getSectionHeaders).stubs().will(returnValue(true));

    ProfDataCollect profDataCollect;
    const void *hdl;
    const void *stubFunc;
    profDataCollect.ProfInit(hdl, stubFunc, true);
    ASSERT_FALSE(KernelContext::Instance().GetLcclFlag());
}

TEST_F(ProfDataCollectTest, RangeReplay_init_no_need_range_replay_expect_true)
{
    ProfDataCollect collect(nullptr, false);
    aclmdlRI modelRI = nullptr;
    rtStream_t stream = nullptr;
    RangeReplayConfig rangeConfig = {true, 0, stream, {"-1", "-1"}};
    MOCKER(&ProfDataCollect::GetThreadRangeConfigMap).stubs().will(returnValue(rangeConfig));
    ASSERT_TRUE(collect.RangeReplay(stream, modelRI));

    rangeConfig = {false, 0, {}};
    MOCKER(&ProfDataCollect::GetThreadRangeConfigMap).stubs().will(returnValue(rangeConfig));
    ASSERT_TRUE(collect.RangeReplay(stream, modelRI));
}

TEST_F(ProfDataCollectTest, RangeReplay_init_mkdir_tmp_path_failed_expect_false)
{
    RangeReplayConfig rangeConfig = {true, 0, nullptr, {"add/dump"}};
    MOCKER(&ProfDataCollect::GetThreadRangeConfigMap).stubs().will(returnValue(rangeConfig));
    ProfDataCollect collect(nullptr, false);
    aclmdlRI modelRI = nullptr;
    rtStream_t stream = nullptr;
    ASSERT_FALSE(collect.RangeReplay(stream, modelRI));
}

TEST_F(ProfDataCollectTest, RangeReplay_init_tmp_path_open_failed_expect_false)
{
    RangeReplayConfig rangeConfig = {true, 0, nullptr, {"add/dump"}};
    MOCKER(&ProfDataCollect::GetThreadRangeConfigMap).stubs().will(returnValue(rangeConfig));
    MOCKER(MkdirRecusively).stubs().will(returnValue(true));
    MOCKER(CheckWriteFilePathValid).stubs().will(returnValue(true));
    ProfDataCollect collect(nullptr, false);
    aclmdlRI modelRI = nullptr;
    rtStream_t stream = nullptr;
    ASSERT_FALSE(collect.RangeReplay(stream, modelRI));
}

TEST_F(ProfDataCollectTest, RangeReplay_pmu_empty_expect_false)
{
    std::string tmpPath = "./tmp";
    MkdirRecusively(tmpPath);
    RangeReplayConfig rangeConfig = {true, 0, &tmpPath, {"add/dump"}};
    MOCKER(&ProfDataCollect::GetThreadRangeConfigMap).stubs().will(returnValue(rangeConfig));
    MOCKER(GetEnv).stubs().will(returnValue(tmpPath));
    MOCKER(aclmdlRIExecuteAsyncImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(aclrtSynchronizeStreamWithTimeoutImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MessageOfProfConfig profMessage;
    profMessage.profWarmUpTimes = 5;
    ProfConfig::Instance().Init(profMessage);

    ProfDataCollect collect(nullptr, false);
    aclmdlRI modelRI = nullptr;
    rtStream_t stream = nullptr;
    ASSERT_FALSE(collect.RangeReplay(stream, modelRI));
    ASSERT_EQ(ProfDataCollect::GetAicoreOutputPath(0), "./tmp/device0/" + std::to_string(getpid()) + "/0");
    RemoveAll(tmpPath);
}

TEST_F(ProfDataCollectTest, RangeReplay_prof_success_expect_true)
{
    std::string tmpPath = "./tmp";
    MkdirRecusively(tmpPath);
    RangeReplayConfig rangeConfig = {true, 0, &tmpPath, {"add/dump"}};
    MOCKER(&ProfDataCollect::GetThreadRangeConfigMap).stubs().will(returnValue(rangeConfig));
    MOCKER(GetEnv).stubs().will(returnValue(tmpPath));
    MOCKER(aclmdlRIExecuteAsyncImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(aclrtSynchronizeStreamWithTimeoutImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&MemoryContext::Backup).stubs().will(returnValue(true));
    MOCKER(&MemoryContext::Restore).stubs().will(returnValue(true));
    MOCKER(prof_drv_start_origin).stubs().will(returnValue(0));
    MOCKER(prof_channel_poll_origin).stubs().will(returnValue(0));
    MessageOfProfConfig profMessage;
    profMessage.profWarmUpTimes = 5;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);

    ProfDataCollect collect(nullptr, false);
    aclmdlRI modelRI = nullptr;
    rtStream_t stream = nullptr;
    ASSERT_TRUE(collect.RangeReplay(stream, modelRI));
    ASSERT_TRUE(IsPathExists("./tmp/device0/" + std::to_string(getpid()) + "/0/output.txt"));
    ASSERT_TRUE(IsPathExists("./tmp/device0/" + std::to_string(getpid()) + "/0/freq.txt"));
    RemoveAll(tmpPath);
}

TEST_F(ProfDataCollectTest, ProfData_range_replay_acl_begin_failed_expect_false)
{
    std::string tmpPath = "./tmp";
    MkdirRecusively(tmpPath);
    ProfConfig::Instance().isRangeReplay_ = true;
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(tmpPath));
    MOCKER(aclmdlRICaptureBeginImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));

    ProfDataCollect profDataCollect;
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    ASSERT_FALSE(profDataCollect.ProfData(stream, func));
    ASSERT_FALSE(IsPathExists("./tmp/op_basic_info.txt"));
    ProfConfig::Instance().isRangeReplay_ = false;
    RemoveAll(tmpPath);
}

TEST_F(ProfDataCollectTest, ProfData_range_replay_save_basic_failed_expect_true)
{
    std::string tmpPath = "./tmp";
    ProfConfig::Instance().isRangeReplay_ = true;
    MOCKER(aclmdlRICaptureBeginImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(tmpPath));
    MOCKER(CheckWriteFilePathValid).stubs().will(returnValue(false));

    ProfDataCollect profDataCollect;
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    ASSERT_TRUE(profDataCollect.ProfData(stream, func));
    ASSERT_FALSE(IsPathExists("./tmp/op_basic_info.txt"));
    ProfConfig::Instance().isRangeReplay_ = false;
}

TEST_F(ProfDataCollectTest, ProfData_range_replay_success_expect_true)
{
    std::string tmpPath = "./tmp";
    MkdirRecusively(tmpPath);
    ProfConfig::Instance().isRangeReplay_ = true;
    MOCKER(aclmdlRICaptureBeginImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(tmpPath));

    ProfDataCollect profDataCollect;
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    ASSERT_TRUE(profDataCollect.ProfData(stream, func));
    ASSERT_TRUE(IsPathExists("./tmp/op_basic_info.txt"));
    ProfConfig::Instance().isRangeReplay_ = false;
    RemoveAll(tmpPath);
}

TEST_F(ProfDataCollectTest, Malloc_Free_L2Cache_Buffer_expect_return_fail)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    DeviceContext::Local().SetSocVersion("Ascend910B1");
    MessageOfProfConfig profMessage;
    profMessage.replayCount = UINT32_INVALID;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    ProfConfig::Instance().Init(profMessage);
    std::string path = "./tmp";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path));
    MOCKER(prof_drv_start_origin).stubs().will(returnValue(-1));
    MOCKER(prof_channel_poll_origin).stubs().will(returnValue(0));
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    std::ofstream writer(path, std::ios::out | std::ios::binary);
    ASSERT_TRUE(writer.is_open());
    writer.close();
    ProfDataCollect profDataCollect;
    MOCKER(&IsExist).stubs().will(returnValue(true));
    ASSERT_FALSE(profDataCollect.ProfData(stream, func));
}

TEST_F(ProfDataCollectTest, test_not_gen_operand_data)
{
    ProfDataCollect profDataCollect;
    profDataCollect.GenRecordData(0, nullptr, OPERAND_RECORD);
    std::string path = path_;
    std::ofstream writer(path, std::ios::out | std::ios::binary);
    ASSERT_TRUE(writer.is_open());
    writer.close();
    MOCKER(aclrtMallocHostImplOrigin).stubs().will(returnValue(0));
    MOCKER(aclrtFreeHostImplOrigin).stubs().will(returnValue(0));
    MOCKER(aclrtMemcpyImplOrigin).stubs().will(returnValue(200));
    testing::internal::CaptureStdout();
    profDataCollect.GenRecordData(1, nullptr, OPERAND_RECORD);
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_NE(capture.find("ERROR"), std::string::npos);
}

TEST_F(ProfDataCollectTest, SaveObject_exptct_return_true)
{
    ProfDataCollect profDataCollect;
    auto p = MakeShared<RegisterContext>();
    MOCKER(&IsExist).stubs().will(returnValue(true));
    MOCKER(&MkdirRecusively).stubs().will(returnValue(true));
    MOCKER(&ElfLoader::LoadHeader).stubs().will(returnValue(true));
    EXPECT_TRUE(profDataCollect.SaveObject(p));
}

TEST_F(ProfDataCollectTest, test_not_gen_operand_data_because_write_data_error)
{
    ProfDataCollect profDataCollect;
    profDataCollect.GenRecordData(0, nullptr, OPERAND_RECORD);

    MOCKER(aclrtMallocHostImplOrigin).stubs().will(returnValue(0));
    MOCKER(aclrtFreeHostImplOrigin).stubs().will(returnValue(0));
    MOCKER(aclrtMemcpyImplOrigin).stubs().will(returnValue(0));
    MOCKER(WriteBinary).stubs().will(returnValue(static_cast<size_t>(0)));
    profDataCollect.GenRecordData(1, nullptr, OPERAND_RECORD);
    auto path = profDataCollect.GetAicoreOutputPath(0);
    EXPECT_EQ(path, "");
}

TEST_F(ProfDataCollectTest, test_gen_operand_data_success)
{
    ProfDataCollect profDataCollect;
    profDataCollect.GenRecordData(0, nullptr, OPERAND_RECORD);
    std::string path = path_;
    std::ofstream writer(path, std::ios::out | std::ios::binary);
    ASSERT_TRUE(writer.is_open());
    writer.close();
    MOCKER(aclrtMallocHostImplOrigin).stubs().will(returnValue(0));
    MOCKER(aclrtFreeHostImplOrigin).stubs().will(returnValue(0));
    MOCKER(aclrtMemcpyImplOrigin).stubs().will(returnValue(0));
    MOCKER(WriteBinary).stubs().will(returnValue(1));
    testing::internal::CaptureStdout();
    profDataCollect.GenRecordData(1, nullptr, OPERAND_RECORD);
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_EQ(capture.find("WARN"), std::string::npos);
    EXPECT_EQ(capture.find("ERROR"), std::string::npos);
    GlobalMockObject::verify();
}

TEST(ProfDataCollect, Ascend910B_kernel_replay_all_success)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    std::string socversion = "Ascend910B1";
    DeviceContext::Local().SetSocVersion(socversion);
    MessageOfProfConfig profMessage;
    profMessage.replayCount = UINT32_INVALID;
    profMessage.profWarmUpTimes = 5;
    std::fill(profMessage.aicPmu, profMessage.aicPmu + 5, 5);
    std::fill(profMessage.aicPmu + 5, profMessage.aicPmu + 16, 0);
    std::fill(profMessage.aivPmu + 5, profMessage.aivPmu + 16, 0);
    std::fill(profMessage.l2CachePmu, profMessage.l2CachePmu + 5, 1);
    ProfConfig::Instance().Init(profMessage);
    std::string path = "./aaa";
    MkdirRecusively(path);
    MOCKER(&ProfConfig::GetOutputPathFromRemote
    ).stubs().will(returnValue(path));
    MOCKER(&MemoryContext::Backup).stubs().will(returnValue(true));
    MOCKER(&MemoryContext::Restore).stubs().will(returnValue(true));
    MOCKER(&MemoryContext::Restore).expects(atMost(12)).will(returnValue(true));

    MOCKER(aclrtMallocImplOrigin).expects(atMost(12)).will(returnValue(ACL_SUCCESS));
    MOCKER(aclrtMemcpyAsyncImplOrigin).expects(atMost(12)).will(returnValue(ACL_SUCCESS));
    MOCKER(aclrtMallocHostImplOrigin).expects(atMost(12)).will(returnValue(ACL_ERROR_BAD_ALLOC));
    MOCKER(aclrtFreeImplOrigin).expects(atMost(12)).will(returnValue(ACL_SUCCESS));
    MOCKER(aclrtSynchronizeStreamImplOrigin).expects(atMost(12)).will(returnValue(ACL_SUCCESS));
    MOCKER(&DeviceContext::GetSocVersion).expects(atMost(20)).will(returnValue(socversion));
    MOCKER(aclrtSynchronizeStreamWithTimeoutImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(prof_drv_start_origin).expects(atMost(20)).will(returnValue(0));
    MOCKER(prof_channel_poll_origin)
            .stubs().will(returnValue(0));
    rtStream_t stream;
    auto func = []() {
        return true;
    };
    uint8_t memInfo[2] = {1, 2};
    ProfDataCollect profDataCollect;
    ASSERT_TRUE(profDataCollect.ProfData(stream, func));
    RemoveAll(path);
}
