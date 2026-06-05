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
#include "runtime/RuntimeOrigin.h"
#include "mockcpp/mockcpp.hpp"
#include "core/LocalProcess.h"
#include "utils/FileSystem.h"
#include "core/FuncSelector.h"
#include "core/DomainSocket.h"
#include "core/LocalProcess.h"
#include "CommonDefines.h"

#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/KernelMatcher.h"
#include "runtime/inject_helpers/BBCountDumper.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "runtime/inject_helpers/InstrReport.h"
#include "runtime/inject_helpers/DBITask.h"
#include "runtime/inject_helpers/RegisterContext.h"

using namespace std;

TEST(rtKernelLaunch, call_function_with_null_input_expect_return)
{
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    void *args = nullptr;
    uint32_t argsSize = 0;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    MOCKER(&func_injection::FunctionLoader::Get).stubs().will(returnValue((void*)nullptr));
    HijackedFuncOfKernelLaunch instance;
    auto ret = instance.Call(stubFunc, blockDim, args, argsSize, smDesc, stm);
    ASSERT_EQ(ret, RT_ERROR_RESERVED);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, call_function_with_null_input_not_coverage_expect_return)
{
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    void *args = nullptr;
    uint32_t argsSize = 0;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    HijackedFuncOfKernelLaunch instance;
    auto ret = instance.Call(stubFunc, blockDim, args, argsSize, smDesc, stm);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, call_function_with_not_null_input_and_error_in_rtmallocorigin_expect_return)
{
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    void *args = nullptr;
    uint32_t argsSize = 0;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    MOCKER(rtMallocOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    MOCKER(rtMemcpyOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    HijackedFuncOfKernelLaunch instance;
    auto ret = instance.Call(stubFunc, blockDim, args, argsSize, smDesc, stm);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, call_function_with_not_null_input_and_error_in_rtmemsetorigin_expect_return)
{
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    void *args = nullptr;
    uint32_t argsSize = 0;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    MOCKER(rtMemsetOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    MOCKER(rtMemcpyOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    HijackedFuncOfKernelLaunch instance;
    auto ret = instance.Call(stubFunc, blockDim, args, argsSize, smDesc, stm);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, call_function_with_null_input_and_not_null_originfunc_expect_return)
{
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    void *args = nullptr;
    uint32_t argsSize = 0;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    MOCKER(rtMemcpyOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    HijackedFuncOfKernelLaunch instance;
    auto ret = instance.Call(stubFunc, blockDim, args, argsSize, smDesc, stm);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, call_function_with_null_input_and_not_null_originfunc_and_write_false_expect_return)
{
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    void *args = nullptr;
    uint32_t argsSize = 0;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    MOCKER(WriteBinary).stubs().will(returnValue(0UL));
    HijackedFuncOfKernelLaunch instance;
    MOCKER(rtMemcpyOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    auto ret = instance.Call(stubFunc, blockDim, args, argsSize, smDesc, stm);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, call_function_msprof_simulator_init)
{
    MOCKER(&write).stubs().will(returnValue(-1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    FuncSelector::Instance().Set(ToolType::PROF);
    std::string r = "";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(r));
    ProfConfig::Instance().profConfig_.isSimulator = true;
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    void *args = nullptr;
    uint32_t argsSize = 0;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    HijackedFuncOfKernelLaunch instance;
    instance.Pre(stubFunc, blockDim, args, argsSize, smDesc, stm);
    auto ret = instance.Call(stubFunc, blockDim, args, argsSize, smDesc, stm);
    instance.Post(ret);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, call_function_msprof_onboard_init)
{
    MOCKER(&write).stubs().will(returnValue(-1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    FuncSelector::Instance().Set(ToolType::PROF);
    std::string r = "";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(r));
    ProfConfig::Instance().profConfig_.isSimulator = false;
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    void *args = nullptr;
    uint32_t argsSize = 0;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    HijackedFuncOfKernelLaunch instance;
    instance.Pre(stubFunc, blockDim, args, argsSize, smDesc, stm);
    auto ret = instance.Call(stubFunc, blockDim, args, argsSize, smDesc, stm);
    instance.Post(ret);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, call_function_msprof_onboard_init_when_timeline_enable_expect_success)
{
    MOCKER(&write).stubs().will(returnValue(-1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    FuncSelector::Instance().Set(ToolType::PROF);
    std::string r = "";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(r));
    ProfConfig::Instance().profConfig_.isSimulator = false;
    ProfConfig::Instance().profConfig_.dbiFlag = DBI_FLAG_INSTR_PROF_END;
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    void *args = nullptr;
    uint32_t argsSize = 0;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    HijackedFuncOfKernelLaunch instance;
    instance.Pre(stubFunc, blockDim, args, argsSize, smDesc, stm);
    auto ret = instance.Call(stubFunc, blockDim, args, argsSize, smDesc, stm);
    instance.Post(ret);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, call_trackit_and_init_prof)
{
    FuncSelector::Instance().Set(ToolType::TEST);
    std::string r = "";
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(r));
    ProfConfig::Instance().profConfig_.isSimulator = false;
    HijackedFuncOfKernelLaunch instance;
    instance.Pre(nullptr, 1, nullptr, 0, nullptr, nullptr);
    auto ret = instance.Call(nullptr, 1, nullptr, 0, nullptr, nullptr);
    instance.Post(ret);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

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

TEST(rtKernelLaunch, call_function_when_get_dev_binary_with_unregistered_stub_func_expect_return)
{
    MockKernelLaunchPre();
    using GetDevBinaryFunc = bool(KernelContext::*)(KernelContext::StubFuncPtr, rtDevBinary_t &, bool) const;
    GetDevBinaryFunc getDevBinary = &KernelContext::GetDevBinary;
    MOCKER(getDevBinary).stubs().will(returnValue(false));

    HijackedFuncOfKernelLaunch instance;
    auto ret = instance.Call(nullptr, 1, nullptr, 0, nullptr, nullptr);
    ASSERT_EQ(ret, RT_ERROR_NONE);

    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, call_function_when_get_section_headers_failed_expect_return)
{
    MockKernelLaunchPre();
    using GetDevBinaryFunc = bool(KernelContext::*)(KernelContext::StubFuncPtr, rtDevBinary_t &, bool) const;
    GetDevBinaryFunc getDevBinary = &KernelContext::GetDevBinary;
    MOCKER(getDevBinary).stubs().will(returnValue(true));
    using GetSectionHeadersFunc = bool(*)(rtDevBinary_t const &, std::map<std::string, Elf64_Shdr> &);
    GetSectionHeadersFunc getSectionHeaders = &GetSectionHeaders;
    MOCKER(getSectionHeaders).stubs().will(returnValue(false));

    HijackedFuncOfKernelLaunch instance;
    auto ret = instance.Call(nullptr, 1, nullptr, 0, nullptr, nullptr);
    ASSERT_EQ(ret, RT_ERROR_NONE);

    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, call_function_when_get_get_last_event_failed_expect_return)
{
    MockKernelLaunchPre();
    using GetDevBinaryFunc = bool(KernelContext::*)(KernelContext::StubFuncPtr, rtDevBinary_t &, bool) const;
    GetDevBinaryFunc getDevBinary = &KernelContext::GetDevBinary;
    MOCKER(getDevBinary).stubs().will(returnValue(true));
    using GetSectionHeadersFunc = bool(*)(rtDevBinary_t const &, std::map<std::string, Elf64_Shdr> &);
    GetSectionHeadersFunc getSectionHeaders = &GetSectionHeaders;
    MOCKER(getSectionHeaders).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetLastLaunchEvent).stubs().will(returnValue(false));

    HijackedFuncOfKernelLaunch instance;
    auto ret = instance.Call(nullptr, 1, nullptr, 0, nullptr, nullptr);
    ASSERT_EQ(ret, RT_ERROR_NONE);

    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, call_function_when_get_get_last_event_success_expect_report_malloc_and_free)
{
    MockKernelLaunchPre();
    using GetDevBinaryFunc = bool(KernelContext::*)(KernelContext::StubFuncPtr, rtDevBinary_t &, bool) const;
    GetDevBinaryFunc getDevBinary = &KernelContext::GetDevBinary;
    MOCKER(getDevBinary).stubs().will(returnValue(true));
    using GetSectionHeadersFunc = bool(*)(rtDevBinary_t const &, std::map<std::string, Elf64_Shdr> &);
    GetSectionHeadersFunc getSectionHeaders = &GetSectionHeaders;
    MOCKER(getSectionHeaders).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetLastLaunchEvent).stubs().will(returnValue(true));

    HijackedFuncOfKernelLaunch instance;
    auto ret = instance.Call(nullptr, 1, nullptr, 0, nullptr, nullptr);
    ASSERT_EQ(ret, RT_ERROR_NONE);

    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, prof_gen_bbfile_dbifile_operandfile_fail)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    MOCKER(&ProfDataCollect::IsBBCountNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsMemoryChartNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsOperandRecordNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, bool(*)(const StubFunc**)).stubs().will(returnValue(false));
    HijackedFuncOfKernelLaunch instance;
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    void *args = nullptr;
    uint32_t argsSize = 0;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    instance.Call(stubFunc, blockDim, args, argsSize, smDesc, stm);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, prof_gen_bbfile_dbifile_operandfile_success)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    MOCKER(&ProfDataCollect::IsBBCountNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsMemoryChartNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsOperandRecordNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, bool(*)(const StubFunc**)).stubs().will(returnValue(true));
    uint8_t* testBuffer = new uint8_t[512];
    MOCKER(&InitMemory, uint8_t*(*)(uint64_t)).stubs().will(returnValue(testBuffer));
    HijackedFuncOfKernelLaunch instance;
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    void *args = nullptr;
    uint32_t argsSize = 0;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    instance.Call(stubFunc, blockDim, args, argsSize, smDesc, stm);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtKernelLaunch, sanitizer_pre_and_expect_post_success)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    MOCKER(&RunDBITask, bool(*)(const StubFunc**)).stubs().will(returnValue(true));
    uint8_t* testBuffer = new uint8_t[512];
    MOCKER(&InitMemory, uint8_t*(*)(uint64_t)).stubs().will(returnValue(testBuffer));
    HijackedFuncOfKernelLaunch instance;
    const void * stubFunc = nullptr;
    uint32_t blockDim = 1;
    void *args = nullptr;
    uint32_t argsSize = 0;
    rtSmDesc_t *smDesc = nullptr;
    rtStream_t stm = nullptr;
    instance.Call(stubFunc, blockDim, args, argsSize, smDesc, stm);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}
