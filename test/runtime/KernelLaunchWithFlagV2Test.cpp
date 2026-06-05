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
#define protected public
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/HijackedFunc.h"
#undef private
#undef protected
#include <dlfcn.h>
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
    vector<uint8_t> mockData(10);
    MOCKER(__sanitizer_init).stubs().will(returnValue(mockData.data()));
    MOCKER(&BBCountDumper::GetMemSize).stubs().will(returnValue(uint64_t(1)));
    MOCKER_CPP(&BBCountDumper::Replace, bool(BBCountDumper::*)(const StubFunc**, uint64_t, const std::string&))
        .stubs()
        .will(returnValue(true));
    MOCKER(rtStreamSynchronizeOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    AddLaunchEventFuncPtr0 funcPtr0 = &KernelContext::AddLaunchEvent;
    MOCKER(funcPtr0).stubs();
    AddLaunchEventFuncPtr1 funcPtr1 = &KernelContext::AddLaunchEvent;
    MOCKER(funcPtr1).stubs();
}


TEST(rtKernelLaunchWithFlagV2, call_pre_func_without_coverage_expect_return)
{
    const void *stubFunc = nullptr;
    uint32_t blockDim = 1;
    rtArgsEx_t *argsInfo = nullptr;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    uint32_t flags = 0;
    const rtTaskCfgInfo_t *cfgInfo = nullptr;
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    instance.Pre(stubFunc, blockDim, argsInfo, smDesc, stm, flags, cfgInfo);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithFlagV2, call_post_func_and_error_in_rtmalloc_expect_return)
{
    const void *stubFunc = nullptr;
    uint32_t blockDim = 1;
    rtArgsEx_t argsInfo;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    uint32_t flags = 0;
    const rtTaskCfgInfo_t *cfgInfo = nullptr;
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    MOCKER(rtMallocOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    MOCKER(rtMemcpyOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    instance.Pre(stubFunc, blockDim, &argsInfo, smDesc, stm, flags, cfgInfo);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithFlagV2, call_post_func_and_error_in_rtmemset_expect_return)
{
    const void *stubFunc = nullptr;
    uint32_t blockDim = 1;
    rtArgsEx_t argsInfo;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    uint32_t flags = 0;
    const rtTaskCfgInfo_t *cfgInfo = nullptr;
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    MOCKER(rtMemsetOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    MOCKER(rtMemcpyOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    instance.Pre(stubFunc, blockDim, &argsInfo, smDesc, stm, flags, cfgInfo);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithFlagV2, call_function_with_null_input_expect_return)
{
    const void *stubFunc = nullptr;
    uint32_t blockDim = 1;
    rtArgsEx_t *argsInfo = nullptr;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    uint32_t flags = 0;
    const rtTaskCfgInfo_t *cfgInfo = nullptr;
    MOCKER(&func_injection::FunctionLoader::Get).stubs().will(returnValue((void*)nullptr));
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    auto ret = instance.Call(stubFunc, blockDim, argsInfo, smDesc, stm, flags, cfgInfo);
    ASSERT_EQ(ret, RT_ERROR_RESERVED);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithFlagV2, set_sanitizer_tracekit_then_call_post_and_expand_args_expect_return_success)
{
    MockKernelLaunchPre();
    const void *stubFunc = nullptr;
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
    uint32_t flags = 0;
    const rtTaskCfgInfo_t *cfgInfo = nullptr;
    KernelMatcher::Config matchConfig;
    BBCountDumper::Instance().Init("./", matchConfig);
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    instance.Pre(stubFunc, blockDim, &argsInfo, smDesc, stm, flags, cfgInfo);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithFlagV2, call_post_func_and_expand_args_expect_return)
{
    const void *stubFunc = nullptr;
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
    uint32_t flags = 0;
    const rtTaskCfgInfo_t *cfgInfo = nullptr;
    KernelMatcher::Config matchConfig;
    BBCountDumper::Instance().Init("./", matchConfig);
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    MOCKER(&BBCountDumper::GetMemSize).stubs().will(returnValue(1));
    instance.Pre(stubFunc, blockDim, &argsInfo, smDesc, stm, flags, cfgInfo);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithFlagV2, call_function_msprof_simulator_init)
{
    MOCKER(&write).stubs().will(returnValue(-1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    FuncSelector::Instance().Set(ToolType::PROF);
    std::string r = "";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(r));
    ProfConfig::Instance().profConfig_.isSimulator = true;
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    rtArgsEx_t argsInfo;
    instance.Pre(nullptr, 1, &argsInfo, nullptr, nullptr, 1, nullptr);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);

    ProfConfig::Instance().profConfig_.isSimulator = false;
    instance.Pre(nullptr, 1, &argsInfo, nullptr, nullptr, 1, nullptr);
    ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithFlagV2, call_function_msprof_simulator_init_when_timeline_enable_expect_success)
{
    MOCKER(&write).stubs().will(returnValue(-1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    FuncSelector::Instance().Set(ToolType::PROF);
    std::string r = "";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(r));
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    rtArgsEx_t argsInfo;

    ProfConfig::Instance().profConfig_.isSimulator = false;
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_END;
    instance.Pre(nullptr, 1, &argsInfo, nullptr, nullptr, 1, nullptr);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithFlagV2, call_trackit_and_init_prof)
{
    std::string r = "";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(r));
    ProfConfig::Instance().profConfig_.isSimulator = true;
    MOCKER_CPP(&BBCountDumper::Replace, bool(BBCountDumper::*)(const StubFunc**, uint64_t, const std::string&))
        .stubs()
        .will(returnValue(true));
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    instance.Pre(nullptr, 1, nullptr, nullptr, nullptr, 1, nullptr);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);

    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithFlagV2, prof_gen_bbfile_and_dbifile_fail)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    MOCKER(&ProfDataCollect::IsBBCountNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsMemoryChartNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsOperandRecordNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, bool(*)(const StubFunc**)).stubs().will(returnValue(false));
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    rtArgsEx_t args{};
    uint32_t flags = 0;

    instance.Call(stubFunc, blockDim, &args, smDesc, stm, flags, nullptr);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithFlagV2, prof_gen_bbfile_and_dbifile_success)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    MOCKER(&ProfDataCollect::IsBBCountNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsMemoryChartNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, bool(*)(const StubFunc**)).stubs().will(returnValue(true));
    uint8_t* testBuffer = new uint8_t[512];
    MOCKER(&InitMemory, uint8_t*(*)(uint64_t)).stubs().will(returnValue(testBuffer));
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    rtArgsEx_t args{};
    uint32_t flags = 0;

    instance.Pre(stubFunc, blockDim, &args, smDesc, stm, flags, nullptr);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

rtError_t RtPlaceholder(const void *, unsigned int, rtArgsEx_t *,
                        rtSmDesc_t *, void *, unsigned int, const rtTaskCfgInfo_t *)
{
    return RT_ERROR_NONE;
}

TEST(rtKernelLaunchWithFlagV2, prof_gen_bbfile_and_dbifile_success1)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    MOCKER(&RunDBITask, bool(*)(const StubFunc**)).stubs().will(returnValue(true));
    uint8_t* testBuffer = new uint8_t[512];
    MOCKER(&InitMemory, uint8_t*(*)(uint64_t)).stubs().will(returnValue(testBuffer));
    MOCKER(&ProfDataCollect::IsBBCountNeedGen).stubs().will(returnValue(false));
    MOCKER(&ProfDataCollect::IsMemoryChartNeedGen).stubs().will(returnValue(true));
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    instance.originfunc_ = RtPlaceholder;
    instance.refreshParamFunc_ = [](){};
    instance.newArgsInfo_.args = nullptr;
    instance.profObj_ = std::make_shared<ProfDataCollect>();
    instance.ProfPost();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunchWithFlagV2, sanitizer_pre_and_expect_post_success)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    MOCKER(&RunDBITask, bool(*)(void**, const uint64_t)).stubs().will(returnValue(true));
    uint8_t* testBuffer = new uint8_t[512];
    MOCKER(&InitMemory, uint8_t*(*)(uint64_t)).stubs().will(returnValue(testBuffer));
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    rtArgsEx_t args{};
    instance.Pre(nullptr, 1, &args, nullptr, nullptr, 0, nullptr);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}
