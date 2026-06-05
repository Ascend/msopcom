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


#include <gtest/gtest.h>
#define private public
#include "runtime/inject_helpers/ProfConfig.h"
#undef private
#include <dlfcn.h>
#include "runtime/HijackedFunc.h"
#include "mockcpp/mockcpp.hpp"
#include "runtime/RuntimeOrigin.h"
#include "core/LocalProcess.h"
#include "core/FuncSelector.h"
#include "core/DomainSocket.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/KernelMatcher.h"
#include "runtime/inject_helpers/BBCountDumper.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "runtime/inject_helpers/InstrReport.h"
#include "runtime/inject_helpers/DBITask.h"
#include "CommonDefines.h"


using namespace std;

inline void MockKernelLaunchPre(void)
{
    MOCKER(Notify).stubs().will(returnValue(0));
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(IsOpProf).stubs().will(returnValue(true));
    vector<uint8_t> mockData(10);
    MOCKER(__sanitizer_init).stubs().will(returnValue(mockData.data()));
    MOCKER(&BBCountDumper::GetMemSize).stubs().will(returnValue(uint64_t(1)));
    MOCKER(rtStreamSynchronizeOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    AddLaunchEventFuncPtr0 funcPtr0 = &KernelContext::AddLaunchEvent;
    MOCKER(funcPtr0).stubs();
    AddLaunchEventFuncPtr1 funcPtr1 = &KernelContext::AddLaunchEvent;
    MOCKER(funcPtr1).stubs();
}

TEST(rtKernelLaunchWithHandleV2, call_pre_func_without_coverage_expect_return)
{
    void *hdl = nullptr;
    const uint64_t tilingKey = 0;
    uint32_t blockDim = 1;
    rtArgsEx_t argsInfo{};
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    const rtTaskCfgInfo_t *cfgInfo = nullptr;
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    instance.Pre(hdl, tilingKey, blockDim, &argsInfo, smDesc, stm, cfgInfo);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithHandleV2, call_post_func_and_error_in_rtmalloc_expect_return)
{
    void *hdl = nullptr;
    const uint64_t tilingKey = 0;
    uint32_t blockDim = 1;
    rtArgsEx_t argsInfo{};
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    const rtTaskCfgInfo_t *cfgInfo = nullptr;
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    MOCKER(rtMallocOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    MOCKER(rtMemcpyOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    instance.Pre(hdl, tilingKey, blockDim, &argsInfo, smDesc, stm, cfgInfo);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithHandleV2, call_post_func_and_error_in_rtmemset_expect_return)
{
    void *hdl = nullptr;
    const uint64_t tilingKey = 0;
    uint32_t blockDim = 1;
    rtArgsEx_t *argsInfo = nullptr;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    const rtTaskCfgInfo_t *cfgInfo = nullptr;
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    MOCKER(rtMemsetOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    MOCKER(rtMemcpyOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    instance.Pre(hdl, tilingKey, blockDim, argsInfo, smDesc, stm, cfgInfo);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithHandleV2, call_function_with_null_input_expect_return)
{
    void *hdl = nullptr;
    const uint64_t tilingKey = 0;
    uint32_t blockDim = 1;
    rtArgsEx_t *argsInfo = nullptr;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    const rtTaskCfgInfo_t *cfgInfo = nullptr;

    MOCKER(&func_injection::FunctionLoader::Get).stubs().will(returnValue((void*)nullptr));
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    auto ret = instance.Call(hdl, tilingKey, blockDim, argsInfo, smDesc, stm, cfgInfo);
    ASSERT_EQ(ret, RT_ERROR_RESERVED);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithHandleV2, set_sanitizer_coverage_call_post_func_and_expand_args_expect_return_success)
{
    MockKernelLaunchPre();
    void *hdl = nullptr;
    const uint64_t tilingKey = 0;
    uint32_t blockDim = 1;
    rtArgsEx_t argsInfo{};
    vector<uint8_t> tmpData(20);
    argsInfo.args = tmpData.data();
    argsInfo.isNoNeedH2DCopy = 0;
    argsInfo.argsSize = 20;
    argsInfo.hasTiling = 1;
    argsInfo.tilingAddrOffset = 0;
    argsInfo.hostInputInfoNum = 1;
    rtHostInputInfo_t tmpInfo{};
    argsInfo.hostInputInfoPtr = &tmpInfo;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    const rtTaskCfgInfo_t *cfgInfo = nullptr;
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    KernelMatcher::Config matchConfig;
    BBCountDumper::Instance().Init("./", matchConfig);

    void *mockStubHdl = tmpData.data();
    MOCKER_CPP(&BBCountDumper::Replace, bool(BBCountDumper::*)(void **, const uint64_t, uint64_t, const std::string&))
        .stubs()
        .will(returnValue(true));
    instance.Pre(hdl, tilingKey, blockDim, &argsInfo, smDesc, stm, cfgInfo);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithHandleV2, call_post_func_and_expand_args_expect_return)
{
    void *hdl = nullptr;
    const uint64_t tilingKey = 0;
    uint32_t blockDim = 1;
    rtArgsEx_t argsInfo{};
    vector<uint8_t> tmpData(20);
    argsInfo.args = tmpData.data();
    argsInfo.isNoNeedH2DCopy = 0;
    argsInfo.argsSize = 20;
    argsInfo.hasTiling = 1;
    argsInfo.tilingAddrOffset = 0;
    argsInfo.hostInputInfoNum = 1;
    rtHostInputInfo_t tmpInfo{};
    argsInfo.hostInputInfoPtr = &tmpInfo;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    const rtTaskCfgInfo_t *cfgInfo = nullptr;
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    KernelMatcher::Config matchConfig;
    BBCountDumper::Instance().Init("./", matchConfig);
    MOCKER_CPP(&BBCountDumper::Replace, bool(BBCountDumper::*)(void **, const uint64_t, uint64_t, const std::string&))
        .stubs()
        .will(returnValue(true));
    instance.Pre(hdl, tilingKey, blockDim, &argsInfo, smDesc, stm, cfgInfo);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithHandleV2, call_function_msprof_simulator_init)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    MOCKER(&write).stubs().will(returnValue(-1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    std::string r;
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(r));
    ProfConfig::Instance().profConfig_.isSimulator = true;
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    rtArgsEx_t args{};
    instance.Pre(nullptr, 0, 1, &args, nullptr, nullptr, nullptr);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);

    ProfConfig::Instance().profConfig_.isSimulator = false;
    instance.Pre(nullptr, 0, 1, nullptr, nullptr, nullptr, nullptr);
    ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithHandleV2, call_function_msprof_onboard_init)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    MOCKER(&write).stubs().will(returnValue(-1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    std::string r;
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(r));
    ProfConfig::Instance().profConfig_.isSimulator = false;
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    rtArgsEx_t args{};
    instance.Pre(nullptr, 0, 1, &args, nullptr, nullptr, nullptr);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);

    ProfConfig::Instance().profConfig_.isSimulator = false;
    instance.Pre(nullptr, 0, 1, nullptr, nullptr, nullptr, nullptr);
    ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithHandleV2, call_trackit_and_init_prof)
{
    std::string r;
    ProfConfig::Instance().profConfig_.isSimulator = true;
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    rtArgsEx_t args{};
    instance.Pre(nullptr, 0, 1, &args, nullptr, nullptr, nullptr);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);

    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithHandleV2, prof_gen_bbfile_and_dbifile_fail)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    MOCKER(&ProfDataCollect::IsBBCountNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsMemoryChartNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsOperandRecordNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, bool(*)(void**, const uint64_t)).stubs().will(returnValue(false));
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    rtArgsEx_t args{};
    instance.Pre(nullptr, 0, 1, &args, nullptr, nullptr, nullptr);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithHandleV2, prof_gen_bbfile_and_dbifile_success)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    MOCKER(&ProfDataCollect::IsBBCountNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsMemoryChartNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, bool(*)(void**, const uint64_t)).stubs().will(returnValue(true));
    uint8_t* testBuffer = new uint8_t[512];
    MOCKER(&InitMemory, uint8_t*(*)(uint64_t)).stubs().will(returnValue(testBuffer));
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    rtArgsEx_t args{};
    instance.Pre(nullptr, 0, 1, &args, nullptr, nullptr, nullptr);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithHandleV2, sanitizer_pre_and_expect_post_success)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    MOCKER(&RunDBITask, bool(*)(void**, const uint64_t)).stubs().will(returnValue(true));
    uint8_t* testBuffer = new uint8_t[512];
    MOCKER(&InitMemory, uint8_t*(*)(uint64_t)).stubs().will(returnValue(testBuffer));
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    rtArgsEx_t args{};
    instance.Pre(nullptr, 0, 1, &args, nullptr, nullptr, nullptr);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}
