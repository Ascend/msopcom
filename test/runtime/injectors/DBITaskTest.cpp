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

#include <string>
#include <vector>
#include <sys/types.h>

#include <gtest/gtest.h>
#include "runtime/inject_helpers/DBITask.h"
#include "mockcpp/mockcpp.hpp"

#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/KernelMatcher.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "runtime/inject_helpers/FuncManager.h"
#include "runtime/inject_helpers/TilingFuncContext.h"
#include "runtime/inject_helpers/RegisterManager.h"
#include "runtime/inject_helpers/ArgsManager.h"
#include "core/BinaryInstrumentation.h"
#include "runtime/RuntimeOrigin.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "utils/FileSystem.h"

using namespace std;

class DBITaskTest : public testing::Test {
public:
    void MockFuncContext()
    {
        regCtx_ = std::make_shared<RegisterContext>();
        string tilingKernelName = "abc";
        string stubKernelName = "bcd";
        MOCKER_CPP(&RegisterManager::GetContext).stubs().will(returnValue(regCtx_));
        MOCKER_CPP(&RegisterContext::GetKernelName).stubs().will(returnValue(tilingKernelName));
        MOCKER_CPP(&RegisterContext::GetRegisterId).stubs().will(returnValue(10UL));
        MOCKER_CPP(&RegisterContext::Save).stubs().will(returnValue(true));
        uint64_t data[5];
        tilingFuncCtx_ = FuncManager::Instance().CreateContext(&data[0], uint64_t(0), &data[0]);
        stubFuncCtx_ = FuncManager::Instance().CreateContext(&data[1], stubKernelName.c_str(), &data[1]);
        ASSERT_NE(stubFuncCtx_, nullptr);
        ASSERT_NE(tilingFuncCtx_, nullptr);
        ArgsContextSP argsBinCtx_ = ArgsManager::Instance().CreateContext(&data[0], &data[1]);
        LaunchParam param{ 1, &data[0], true, 1};
        launchCtx_ = make_shared<LaunchContext>(tilingFuncCtx_, argsBinCtx_, param);
    }

    void TearDown() override
    {
        stubFuncCtx_.reset();
        tilingFuncCtx_.reset();
        launchCtx_.reset();
        DBITaskConfig::Instance().Reset();
        GlobalMockObject::verify();
    }

    FuncContextSP tilingFuncCtx_;
    FuncContextSP stubFuncCtx_;
    LaunchContextSP launchCtx_;
    RegisterContextSP regCtx_;
};

TEST_F(DBITaskTest, set_enable_config_expect_enable_success)
{
    BIType type = BIType::CUSTOMIZE;
    string pluginPath = "abc";
    KernelMatcher::Config config{};
    DBITaskConfig::Instance().Init(type, pluginPath, config);

    uint64_t launchId = 0;
    string kernelName = "";
    EXPECT_TRUE(DBITaskConfig::Instance().IsEnabled(launchId, kernelName));
    config.launchId = 1;
    DBITaskConfig::Instance().Init(type, pluginPath, config);
    EXPECT_FALSE(DBITaskConfig::Instance().IsEnabled(launchId, kernelName));
    EXPECT_TRUE(DBITaskConfig::Instance().IsEnabled(config.launchId, kernelName));
}

TEST_F(DBITaskTest, input_different_tiling_expect_get_new_dbi_task)
{
    BIType type = BIType::CUSTOMIZE;
    string tilingKey = "123";
    uint64_t regId = 0;
    auto task00 = DBITaskFactory::Instance().GetOrCreate(regId, tilingKey, type);
    tilingKey = "321";
    auto task01 = DBITaskFactory::Instance().GetOrCreate(regId, tilingKey, type);
    ASSERT_NE(task00, task01);

    tilingKey = "123";
    auto task02 = DBITaskFactory::Instance().GetOrCreate(regId, tilingKey, type);
    ASSERT_EQ(task00, task02);
}

TEST_F(DBITaskTest, input_different_stub_func_get_new_dbi_task)
{
    BIType type = BIType::CUSTOMIZE;
    uint64_t stubFunc0 {0};
    uint64_t stubFunc1 {0};
    uint64_t regId = 0;
    auto task00 = DBITaskFactory::Instance().GetOrCreate(regId, &stubFunc0, type);
    auto task01 = DBITaskFactory::Instance().GetOrCreate(regId, &stubFunc1, type);
    ASSERT_NE(task00, task01);
    auto task02 = DBITaskFactory::Instance().GetOrCreate(regId, &stubFunc0, type);
    ASSERT_EQ(task00, task02);
}

TEST_F(DBITaskTest, test_destroy_expect_no_core_dump)
{
    BIType type = BIType::CUSTOMIZE;
    string tilingKey{"123"};
    uint64_t stubFunc {0};
    auto task00 = DBITaskFactory::Instance().GetOrCreate(0, tilingKey, type);
    auto task01 = DBITaskFactory::Instance().GetOrCreate(1, &stubFunc, type);
    DBITaskFactory::Instance().Destroy(0);
    DBITaskFactory::Instance().Destroy(1);
    DBITaskFactory::Instance().Destroy(2);
}

TEST_F(DBITaskTest, dbi_task_run_with_handle_with_get_launch_event_failed_expect_return_not_replaced)
{
    MOCKER(&KernelContext::GetLaunchEvent).stubs().will(returnValue(false));

    BIType type = BIType::CUSTOMIZE;
    uint64_t regId = 0;
    string tilingKey{"123"};
    auto task00 = DBITaskFactory::Instance().GetOrCreate(regId, tilingKey, type);

    void *handle = nullptr;
    task00->Run(&handle, 0);
    EXPECT_EQ(handle, nullptr);
}

TEST_F(DBITaskTest, dbi_task_run_with_handle_with_mkdir_recusively_failed_expect_return_not_replaced)
{
    MOCKER(&KernelContext::GetLaunchEvent).stubs().will(returnValue(true));
    MOCKER(&MkdirRecusively).stubs().will(returnValue(false));

    BIType type = BIType::CUSTOMIZE;
    uint64_t regId = 0;
    string tilingKey{"123"};
    auto task00 = DBITaskFactory::Instance().GetOrCreate(regId, tilingKey, type);

    void *handle = nullptr;
    task00->Run(&handle, 0);
    EXPECT_EQ(handle, nullptr);
}

TEST_F(DBITaskTest, dbi_task_run_with_handle_with_dump_kernel_object_failed_expect_return_not_replaced)
{
    MOCKER(&KernelContext::GetLaunchEvent).stubs().will(returnValue(true));
    MOCKER(&MkdirRecusively).stubs().will(returnValue(true));
    bool (KernelContext::*dumpKernelObject)(uint64_t, const std::string &) = &KernelContext::DumpKernelObject;
    MOCKER(dumpKernelObject).stubs().will(returnValue(false));

    BIType type = BIType::CUSTOMIZE;
    uint64_t regId = 0;
    string tilingKey{"123"};
    auto task00 = DBITaskFactory::Instance().GetOrCreate(regId, tilingKey, type);

    void *handle = nullptr;
    task00->Run(&handle, 0);
    EXPECT_EQ(handle, nullptr);
}

TEST_F(DBITaskTest, test_dbi_task_multiple_run_expect_return_same_result)
{
    MOCKER(&DeviceContext::GetSocVersion).stubs().will(returnValue(string("Ascend910B")));
    KernelContext::LaunchEvent event;
    event.tilingKey = 123;
    bool(KernelContext::*funcPtr)(uint64_t, const string&) = &KernelContext::DumpKernelObject;
    MOCKER(funcPtr).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetLaunchEvent).stubs().with(any(), outBound(event)).will(returnValue(true));
    MOCKER(&DBITask::Convert).stubs().will(returnValue(true));
    vector<char> fakeData(5);
    void *fakeHandle = fakeData.data();
    MOCKER(&MkdirRecusively).stubs().will(returnValue(true));
    MOCKER_CPP(&KernelReplaceTask::Run,
               bool(KernelReplaceTask::*)(void **, uint64_t, bool)).stubs()
        .with(outBoundP(&fakeHandle, any()))
        .will(returnValue(true));
    MOCKER(&KernelReplaceTask::GetHandle).stubs()
        .will(returnValue(fakeHandle));

    BIType type = BIType::CUSTOMIZE;
    string tilingKey{"123"};
    uint64_t regId = 0;
    auto task00 = DBITaskFactory::Instance().GetOrCreate(regId, tilingKey, type);

    char *handle{nullptr};
    task00->Run((void**)&handle, 0);
    EXPECT_EQ(handle, fakeHandle);
    task00->Run((void**)&handle, 0);
    EXPECT_EQ(handle, fakeHandle);
}

TEST_F(DBITaskTest, dbi_task_run_with_stub_func_with_get_launch_event_failed_expect_return_not_replaced)
{
    bool (DBITask::*runWithHandle)(void **, uint64_t, bool) = &DBITask::Run;
    MOCKER(runWithHandle).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetLaunchEvent).stubs().will(returnValue(false));

    BIType type = BIType::CUSTOMIZE;
    uint64_t regId = 0;
    uint64_t stubFunc {0};
    auto task00 = DBITaskFactory::Instance().GetOrCreate(regId, &stubFunc, type);

    const void *stubPtr = &stubFunc;
    void *handle = nullptr;
    task00->Run(&handle, &stubPtr, 0);
    EXPECT_EQ(stubPtr, &stubFunc);
}

TEST_F(DBITaskTest, dbi_task_run_with_stub_func_with_get_stub_func_info_failed_expect_return_not_replaced)
{
    bool (DBITask::*runWithHandle)(void **, uint64_t, bool) = &DBITask::Run;
    MOCKER(runWithHandle).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetLaunchEvent).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetStubFuncInfo).stubs().will(returnValue(false));

    BIType type = BIType::CUSTOMIZE;
    uint64_t regId = 0;
    uint64_t stubFunc {0};
    auto task00 = DBITaskFactory::Instance().GetOrCreate(regId, &stubFunc, type);

    const void *stubPtr = &stubFunc;
    void *handle = nullptr;
    task00->Run(&handle, &stubPtr, 0);
    EXPECT_EQ(stubPtr, &stubFunc);
}

TEST_F(DBITaskTest, dbi_task_run_with_stub_func_with_rt_function_register_failed_expect_return_not_replaced)
{
    bool (DBITask::*runWithHandle)(void **, uint64_t, bool) = &DBITask::Run;
    MOCKER(runWithHandle).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetLaunchEvent).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetStubFuncInfo).stubs().will(returnValue(true));
    MOCKER(&rtFunctionRegisterOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));

    BIType type = BIType::CUSTOMIZE;
    uint64_t regId = 0;
    uint64_t stubFunc {0};
    auto task00 = DBITaskFactory::Instance().GetOrCreate(regId, &stubFunc, type);

    const void *stubPtr = &stubFunc;
    void *handle = nullptr;
    task00->Run(&handle, &stubPtr, 0);
    EXPECT_EQ(stubPtr, &stubFunc);
}

TEST_F(DBITaskTest, dbi_task_run_with_stub_func_with_all_branches_success_expect_return_replaced)
{
    bool (DBITask::*runWithHandle)(void **, uint64_t, bool) = &DBITask::Run;
    MOCKER(runWithHandle).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetLaunchEvent).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetStubFuncInfo).stubs().will(returnValue(true));
    MOCKER(&rtFunctionRegisterOrigin).stubs().will(returnValue(RT_ERROR_NONE));

    BIType type = BIType::CUSTOMIZE;
    uint64_t regId = 0;
    uint64_t stubFunc {0};
    auto task00 = DBITaskFactory::Instance().GetOrCreate(regId, &stubFunc, type);

    const void *stubPtr = &stubFunc;
    void *handle = nullptr;
    task00->Run(&handle, &stubPtr, 0);
    EXPECT_NE(stubPtr, &stubFunc);
}


TEST_F(DBITaskTest, mock_valid_launch_context_then_run_task_expect_return_replaced)
{
    MockFuncContext();
    MOCKER(&DBITask::Convert).stubs().will(returnValue(true));
    MOCKER_CPP(&RegisterContext::Clone).stubs().will(returnValue(regCtx_));

    MOCKER_CPP(&aclrtBinaryGetFunctionByEntryImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    string pluginPath = "pluginPath";
    KernelMatcher::Config config{};
    BIType type = BIType::CUSTOMIZE;
    uint64_t regId = 0;
    string kernelName = launchCtx_->GetFuncContext()->GetKernelName();
    auto task00 = DBITaskFactory::Instance().GetOrCreate(regId, kernelName, type);
    DBITaskConfig::Instance().Init(type, pluginPath, config);

    ASSERT_NE(task00->Run(launchCtx_), nullptr);
}

TEST_F(DBITaskTest, set_convert_failed_then_test_dbi_task_run_expect_failed)
{
    MOCKER(&DeviceContext::GetSocVersion).stubs().will(returnValue(string("Ascend910B")));
    KernelContext::LaunchEvent event;
    bool(KernelContext::*funcPtr)(uint64_t, const string&) = &KernelContext::DumpKernelObject;
    MOCKER(funcPtr).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetLaunchEvent).stubs().with(any(), outBound(event)).will(returnValue(true));
    MOCKER(&DBITask::Convert).stubs().will(returnValue(false));
    MOCKER(&MkdirRecusively).stubs().will(returnValue(true));

    BIType type = BIType::CUSTOMIZE;
    string tilingKey{"123"};
    uint64_t regId = 0;
    auto task00 = DBITaskFactory::Instance().GetOrCreate(regId, tilingKey, type);

    char *handle{nullptr};
    task00->Run((void**)&handle, 0);
    EXPECT_EQ(handle, nullptr);
}

TEST_F(DBITaskTest, init_memory)
{
    uint8_t *memInfo = nullptr;
    MOCKER(rtMallocOrigin)
    .stubs().will(returnValue(RT_ERROR_NONE));

    MOCKER(rtMemsetOrigin)
            .stubs().will(returnValue(RT_ERROR_RESERVED));
    memInfo = InitMemory(1);
}

TEST_F(DBITaskTest, RunDBITask_stub_task_success)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");
    using GetDevBinaryFunc = bool(KernelContext::*)(KernelContext::StubFuncPtr, rtDevBinary_t &, bool) const;
    GetDevBinaryFunc getDevBinary = &KernelContext::GetDevBinary;
    MOCKER(getDevBinary).stubs().will(returnValue(true));
    using GetLaunchEventFunc = bool (KernelContext::*)(uint64_t launchId, KernelContext::LaunchEvent &event) const;
    GetLaunchEventFunc getLaunchEvent = &KernelContext::GetLaunchEvent;
    MOCKER(getLaunchEvent).stubs().will(returnValue(true));
    using UpdatePcStartAddrByDbiFunc = bool (KernelContext::DeviceContext::*)
        (uint64_t launchId, KernelContext::StubFuncArgs const &args);
    UpdatePcStartAddrByDbiFunc updatePcStartAddrByDbi = &KernelContext::DeviceContext::UpdatePcStartAddrByDbi;
    MOCKER(updatePcStartAddrByDbi).stubs().will(returnValue(true));

    BIType type = BIType::CUSTOMIZE;
    string pluginPath = "pluginPath";
    KernelMatcher::Config config{};
    DBITaskConfig::Instance().Init(type, pluginPath, config);
    MOCKER(HasStaticStub).stubs().will(returnValue(false));

    bool (DBITask::*run)(void **, const void **, uint64_t) = &DBITask::Run;
    MOCKER(run).stubs().will(returnValue(true));

    const void *stubFunc = nullptr;
    EXPECT_TRUE(RunDBITask(&stubFunc));
}

TEST_F(DBITaskTest, RunDBITask_stub_task_failed_when_platform_not_support)
{
    DeviceContext::Local().SetSocVersion("Ascend310P");
    const void *stubFunc = nullptr;
    EXPECT_FALSE(RunDBITask(&stubFunc));
}

TEST_F(DBITaskTest, RunDBITask_stub_task_failed_when_get_dev_binary_failed)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");
    const void *stubFunc = nullptr;
    EXPECT_FALSE(RunDBITask(&stubFunc));
}

TEST_F(DBITaskTest, RunDBITask_stub_task_failed_when_run_failed)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");
    using GetDevBinaryFunc = bool(KernelContext::*)(KernelContext::StubFuncPtr, rtDevBinary_t &, bool) const;
    GetDevBinaryFunc getDevBinary = &KernelContext::GetDevBinary;
    MOCKER(getDevBinary).stubs().will(returnValue(true));

    BIType type = BIType::CUSTOMIZE;
    string pluginPath = "pluginPath";
    KernelMatcher::Config config{};
    DBITaskConfig::Instance().Init(type, pluginPath, config);
    MOCKER(HasStaticStub).stubs().will(returnValue(false));

    const void *stubFunc = nullptr;
    EXPECT_FALSE(RunDBITask(&stubFunc));
}

TEST_F(DBITaskTest, RunDBITask_tiling_task_success)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");
    using GetDevBinaryFunc = bool(KernelContext::*)(KernelContext::KernelHandlePtr, rtDevBinary_t &, bool) const;
    GetDevBinaryFunc getDevBinary = &KernelContext::GetDevBinary;
    MOCKER(getDevBinary).stubs().will(returnValue(true));
    using GetLaunchEventFunc = bool (KernelContext::*)(uint64_t launchId, KernelContext::LaunchEvent &event) const;
    GetLaunchEventFunc getLaunchEvent = &KernelContext::GetLaunchEvent;
    MOCKER(getLaunchEvent).stubs().will(returnValue(true));
    using UpdatePcStartAddrByDbiFunc = bool (KernelContext::DeviceContext::*)
        (uint64_t launchId, KernelContext::KernelHandleArgs const &args);
    UpdatePcStartAddrByDbiFunc updatePcStartAddrByDbi = &KernelContext::DeviceContext::UpdatePcStartAddrByDbi;
    MOCKER(updatePcStartAddrByDbi).stubs().will(returnValue(true));

    BIType type = BIType::CUSTOMIZE;
    string pluginPath = "pluginPath";
    KernelMatcher::Config config{};
    DBITaskConfig::Instance().Init(type, pluginPath, config);
    MOCKER(HasStaticStub).stubs().will(returnValue(false));

    bool (DBITask::*run)(void **, uint64_t, bool) = &DBITask::Run;
    MOCKER(run).stubs().will(returnValue(true));

    void *hdl = nullptr;
    const uint64_t tilingKey = 1;
    EXPECT_TRUE(RunDBITask(&hdl, tilingKey));
}

TEST_F(DBITaskTest, RunDBITask_tiling_task_failed_when_platform_not_support)
{
    DeviceContext::Local().SetSocVersion("Ascend310P");
    void *hdl = nullptr;
    const uint64_t tilingKey = 1;
    EXPECT_FALSE(RunDBITask(&hdl, tilingKey));
}

TEST_F(DBITaskTest, RunDBITask_tiling_task_failed_when_get_dev_binary_failed)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");
    void *hdl = nullptr;
    const uint64_t tilingKey = 1;
    EXPECT_FALSE(RunDBITask(&hdl, tilingKey));
}

TEST_F(DBITaskTest, RunDBITask_tiling_task_failed_when_run_failed)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");
    using GetDevBinaryFunc = bool(KernelContext::*)(KernelContext::KernelHandlePtr, rtDevBinary_t &, bool) const;
    GetDevBinaryFunc getDevBinary = &KernelContext::GetDevBinary;
    MOCKER(getDevBinary).stubs().will(returnValue(true));

    BIType type = BIType::CUSTOMIZE;
    string pluginPath = "pluginPath";
    KernelMatcher::Config config{};
    DBITaskConfig::Instance().Init(type, pluginPath, config);
    MOCKER(HasStaticStub).stubs().will(returnValue(false));

    void *hdl = nullptr;
    const uint64_t tilingKey = 1;
    EXPECT_FALSE(RunDBITask(&hdl, tilingKey));
}

TEST_F(DBITaskTest, mock_launch_context_then_RunDBITask_expect_success)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");

    BIType type = BIType::CUSTOMIZE;
    string pluginPath = "pluginPath";
    KernelMatcher::Config config{};
    DBITaskConfig::Instance().Init(type, pluginPath, config);
    MOCKER(HasStaticStub).stubs().will(returnValue(false));

    MockFuncContext();
    FuncContextSP (DBITask::*run)(const LaunchContextSP &) = &DBITask::Run;
    MOCKER(run).stubs().will(returnValue(stubFuncCtx_));

    EXPECT_NE(RunDBITask(launchCtx_), nullptr);
}

TEST_F(DBITaskTest, mock_launch_context_and_run_failed_then_RunDBITask_expect_nullptr)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");

    BIType type = BIType::CUSTOMIZE;
    string pluginPath = "pluginPath";
    KernelMatcher::Config config{};
    DBITaskConfig::Instance().Init(type, pluginPath, config);
    MOCKER(HasStaticStub).stubs().will(returnValue(false));

    MockFuncContext();
    FuncContextSP (DBITask::*run)(const LaunchContextSP &) = &DBITask::Run;
    MOCKER(run).stubs().will(returnValue(FuncContextSP(nullptr)));

    EXPECT_EQ(RunDBITask(launchCtx_), nullptr);
}

// 静态桩场景：开启 keepTaskOutputs(INJ_LOG_LEVEL=0) 后，kernel.o 落盘到 launch 目录
TEST_F(DBITaskTest, RunDBITask_launch_ctx_static_stub_with_keep_outputs_expect_dump)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");
    BIType type = BIType::CUSTOMIZE;
    string pluginPath = "pluginPath";
    KernelMatcher::Config config{};
    DBITaskConfig::Instance().Init(type, pluginPath, config);
    DBITaskConfig::Instance().KeepTaskOutputs();

    regCtx_ = std::make_shared<RegisterContext>();
    string tilingKernelName = "abc";
    MOCKER_CPP(&RegisterManager::GetContext).stubs().will(returnValue(regCtx_));
    MOCKER_CPP(&RegisterContext::GetKernelName).stubs().will(returnValue(tilingKernelName));
    MOCKER_CPP(&RegisterContext::GetRegisterId).stubs().will(returnValue(10UL));
    MOCKER_CPP(&RegisterContext::HasStaticStub).stubs().will(returnValue(true));
    MOCKER_CPP(&RegisterContext::Save).expects(exactly(1)).will(returnValue(true));
    MOCKER(&MkdirRecusively).stubs().will(returnValue(true));

    uint64_t data[5];
    tilingFuncCtx_ = FuncManager::Instance().CreateContext(&data[0], uint64_t(0), &data[0]);
    ArgsContextSP argsBinCtx = ArgsManager::Instance().CreateContext(&data[0], &data[1]);
    LaunchParam param{ 1, &data[0], true, 1};
    launchCtx_ = make_shared<LaunchContext>(tilingFuncCtx_, argsBinCtx, param);

    auto funcCtx = RunDBITask(launchCtx_);
    EXPECT_EQ(funcCtx, nullptr);
    // TearDown 中 GlobalMockObject::verify() 校验 RegisterContext::Save 恰好被调用一次
}

// 静态桩场景：未开启 keepTaskOutputs 时，不产生额外落盘文件（行为与现状一致）
TEST_F(DBITaskTest, RunDBITask_launch_ctx_static_stub_without_keep_outputs_expect_no_dump)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");
    BIType type = BIType::CUSTOMIZE;
    string pluginPath = "pluginPath";
    KernelMatcher::Config config{};
    DBITaskConfig::Instance().Init(type, pluginPath, config);
    // 显式关闭，避免受环境变量 INJ_LOG_LEVEL 或前序测试状态影响
    DBITaskConfig::Instance().keepTaskOutputs_ = false;

    regCtx_ = std::make_shared<RegisterContext>();
    string tilingKernelName = "abc";
    MOCKER_CPP(&RegisterManager::GetContext).stubs().will(returnValue(regCtx_));
    MOCKER_CPP(&RegisterContext::GetKernelName).stubs().will(returnValue(tilingKernelName));
    MOCKER_CPP(&RegisterContext::GetRegisterId).stubs().will(returnValue(10UL));
    MOCKER_CPP(&RegisterContext::HasStaticStub).stubs().will(returnValue(true));
    MOCKER_CPP(&RegisterContext::Save).expects(exactly(0));

    uint64_t data[5];
    tilingFuncCtx_ = FuncManager::Instance().CreateContext(&data[0], uint64_t(0), &data[0]);
    ArgsContextSP argsBinCtx = ArgsManager::Instance().CreateContext(&data[0], &data[1]);
    LaunchParam param{ 1, &data[0], true, 1};
    launchCtx_ = make_shared<LaunchContext>(tilingFuncCtx_, argsBinCtx, param);

    auto funcCtx = RunDBITask(launchCtx_);
    EXPECT_EQ(funcCtx, nullptr);
    // TearDown 中 GlobalMockObject::verify() 校验 RegisterContext::Save 未被调用
}

// rt 级 launch (rtKernelLaunch) 静态桩场景：开启 keepTaskOutputs 后落盘 kernel.o
TEST_F(DBITaskTest, RunDBITask_stub_func_static_stub_with_keep_outputs_expect_dump)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");
    BIType type = BIType::CUSTOMIZE;
    string pluginPath = "pluginPath";
    KernelMatcher::Config config{};
    DBITaskConfig::Instance().Init(type, pluginPath, config);
    DBITaskConfig::Instance().KeepTaskOutputs();

    rtDevBinary_t binary{};
    char data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    binary.data = data;
    binary.length = sizeof(data);
    using GetDevBinaryFunc = bool(KernelContext::*)(KernelContext::StubFuncPtr, rtDevBinary_t &, bool) const;
    GetDevBinaryFunc getDevBinary = &KernelContext::GetDevBinary;
    MOCKER(getDevBinary).stubs().with(any(), outBound(binary), any()).will(returnValue(true));
    MOCKER(HasStaticStub).stubs().will(returnValue(true));
    MOCKER(&MkdirRecusively).stubs().will(returnValue(true));
    MOCKER(&WriteBinary).expects(exactly(1)).will(returnValue(size_t(sizeof(data))));

    const void *stubFunc = nullptr;
    EXPECT_FALSE(RunDBITask(&stubFunc));
    // TearDown 中 GlobalMockObject::verify() 校验 WriteBinary 被调用一次（落盘 kernel.o）
}

// rt 级 launch (rtKernelLaunchWithHandleV2) 静态桩场景：开启 keepTaskOutputs 后落盘 kernel.o
TEST_F(DBITaskTest, RunDBITask_handle_static_stub_with_keep_outputs_expect_dump)
{
    DeviceContext::Local().SetSocVersion("Ascend910B");
    BIType type = BIType::CUSTOMIZE;
    string pluginPath = "pluginPath";
    KernelMatcher::Config config{};
    DBITaskConfig::Instance().Init(type, pluginPath, config);
    DBITaskConfig::Instance().KeepTaskOutputs();

    rtDevBinary_t binary{};
    char data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    binary.data = data;
    binary.length = sizeof(data);
    using GetDevBinaryFunc = bool(KernelContext::*)(KernelContext::KernelHandlePtr, rtDevBinary_t &, bool) const;
    GetDevBinaryFunc getDevBinary = &KernelContext::GetDevBinary;
    MOCKER(getDevBinary).stubs().with(any(), outBound(binary), any()).will(returnValue(true));
    MOCKER(HasStaticStub).stubs().will(returnValue(true));
    MOCKER(&MkdirRecusively).stubs().will(returnValue(true));
    MOCKER(&WriteBinary).expects(exactly(1)).will(returnValue(size_t(sizeof(data))));

    void *hdl = nullptr;
    const uint64_t tilingKey = 1;
    EXPECT_FALSE(RunDBITask(&hdl, tilingKey));
    // TearDown 中 GlobalMockObject::verify() 校验 WriteBinary 被调用一次（落盘 kernel.o）
}
