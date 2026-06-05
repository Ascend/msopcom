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
#include <cstdlib>
#include "mockcpp/mockcpp.hpp"
#define private public
#define protected public
#include "runtime/inject_helpers/ProfConfig.h"
#include "acl_rt_impl/HijackedFunc.h"
#undef private
#undef protected
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "injectors/ContextMockHelper.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/DBITask.h"
#include "core/FunctionLoader.h"
using namespace std;

class HijackedFuncOfAclrtLaunchKernelWithConfigTest : public ContextMockHelper {
};

TEST_F(HijackedFuncOfAclrtLaunchKernelWithConfigTest, mock_valid_hijacked_input_then_test_call_expect_ok)
{
    const std::string soc = "Ascend910B4";
    using namespace func_injection::register_function;
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    aclrtStream stream = &placeholder_;
    HijackedFuncOfAclrtLaunchKernelWithConfigImpl inst;
    MOCKER(&FunctionRegister::Get).stubs().will(returnValue(stream));
    MOCKER(&aclrtGetDeviceImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtGetUserDevIdByLogicDevIdImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtGetFunctionAddrImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtGetSocNameImplOrigin).stubs().will(returnValue(soc.c_str()));
    MOCKER(&aclrtMallocImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(&rtGetL2CacheOffsetOrigin).stubs().will(returnValue(ACL_SUCCESS));
    inst.originfunc_ = [](aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream,
                          aclrtLaunchKernelCfg *cfg, aclrtArgsHandle argsHandle, void *reserve) { return ACL_SUCCESS; };
    ASSERT_EQ(inst.Call(funcHandle_, 3, stream, nullptr, argsHandle_, nullptr), ACL_SUCCESS);
}

TEST_F(HijackedFuncOfAclrtLaunchKernelWithConfigTest, input_nullptr_then_test_call_expect_no_core_dump)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    HijackedFuncOfAclrtLaunchKernelWithConfigImpl inst;
    ASSERT_EQ(inst.Call(nullptr, 3, nullptr, nullptr, nullptr, nullptr), ACL_SUCCESS);
}

TEST_F(HijackedFuncOfAclrtLaunchKernelWithConfigTest, call_function_msprof_simulator_init)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    std::string r;
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(r));
    ProfConfig::Instance().profConfig_.isSimulator = true;
    HijackedFuncOfAclrtLaunchKernelWithConfigImpl inst;
    MOCKER(aclrtSynchronizeStreamImplOrigin)
            .stubs().will(returnValue(ACL_SUCCESS));
    inst.profObj_ = std::make_shared<ProfDataCollect>(nullptr);
    inst.Pre(nullptr, 3, nullptr, nullptr, nullptr, nullptr);
    EXPECT_TRUE(inst.Post(ACL_SUCCESS) == ACL_SUCCESS);
    EXPECT_TRUE(inst.profObj_ != nullptr);
    GlobalMockObject::verify();
}

TEST_F(HijackedFuncOfAclrtLaunchKernelWithConfigTest, prof_gen_bbfile_and_dbifile_fail)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    MOCKER(&ProfDataCollect::IsBBCountNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsMemoryChartNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsOperandRecordNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, FuncContextSP(*)(const LaunchContextSP &)).stubs().will(returnValue(false));
    aclrtStream stream = &placeholder_;
    HijackedFuncOfAclrtLaunchKernelWithConfigImpl inst;
    inst.profObj_ = std::make_shared<ProfDataCollect>(nullptr);
    inst.Pre(funcHandle_, 3, stream, nullptr, argsHandle_, nullptr);
    EXPECT_TRUE(inst.Post(ACL_SUCCESS) == ACL_SUCCESS);
    EXPECT_TRUE(inst.profObj_ != nullptr);
    GlobalMockObject::verify();
}

TEST_F(HijackedFuncOfAclrtLaunchKernelWithConfigTest, test_operand_record_expand_args_failed)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    MOCKER(&ProfDataCollect::IsOperandRecordNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, FuncContextSP(*)(const LaunchContextSP &)).stubs().will(returnValue(false));
    uint8_t* testBuffer = nullptr;
    MOCKER(&InitMemory, uint8_t*(*)(uint64_t)).stubs().will(returnValue(testBuffer));
    MOCKER(&rtGetL2CacheOffsetOrigin).stubs().will(returnValue(ACL_SUCCESS));
    HijackedFuncOfAclrtLaunchKernelWithConfigImpl inst;
    auto func = []() -> void {};
    inst.refreshParamFunc_ = func;
    aclrtStream stream = &placeholder_;
    inst.profObj_ = std::make_shared<ProfDataCollect>(nullptr);
    inst.Pre(funcHandle_, 3, stream, nullptr, argsHandle_, nullptr);
    testing::internal::CaptureStdout();
    inst.ProfPost();
    string capture = testing::internal::GetCapturedStdout();
    ASSERT_TRUE(capture.find("ExpandArgs failed"));
    GlobalMockObject::verify();
}
