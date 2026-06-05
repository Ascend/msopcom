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
using namespace std;

class HijackedFuncOfAclrtLaunchKernelTest : public ContextMockHelper {
};

TEST_F(HijackedFuncOfAclrtLaunchKernelTest, mock_valid_hijacked_input_then_test_call_expect_ok)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    aclrtStream stream = &placeholder_;
    HijackedFuncOfAclrtLaunchKernelImpl inst;
    uint32_t aaa = 10;
    ASSERT_EQ(inst.Call(funcHandle_, 3, &aaa, 4, stream), ACL_SUCCESS);
}

TEST_F(HijackedFuncOfAclrtLaunchKernelTest, input_nullptr_then_test_call_expect_no_core_dump)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    HijackedFuncOfAclrtLaunchKernelImpl inst;
    testing::internal::CaptureStdout();
    ASSERT_EQ(inst.Call(nullptr, 3, nullptr, 0, 0), ACL_SUCCESS);
    string capture = testing::internal::GetCapturedStdout();
    ASSERT_EQ(capture.find("ERROR"), std::string::npos);
}

TEST_F(HijackedFuncOfAclrtLaunchKernelTest, call_function_msprof_simulator_init)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    std::string r;
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(r));
    ProfConfig::Instance().profConfig_.isSimulator = true;
    HijackedFuncOfAclrtLaunchKernelImpl inst;
    MOCKER(aclrtSynchronizeStreamImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    inst.profObj_ = std::make_shared<ProfDataCollect>(nullptr);
    inst.Pre(nullptr, 3, nullptr, 0, nullptr);
    EXPECT_TRUE(inst.Post(ACL_SUCCESS) == ACL_SUCCESS);
    EXPECT_TRUE(inst.profObj_ != nullptr);
    GlobalMockObject::verify();
}

TEST_F(HijackedFuncOfAclrtLaunchKernelTest, prof_gen_bbfile_and_dbifile_fail)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    MOCKER(&ProfDataCollect::IsBBCountNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsMemoryChartNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsOperandRecordNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, FuncContextSP(*)(const LaunchContextSP &)).stubs().will(returnValue(false));
    aclrtStream stream = &placeholder_;
    HijackedFuncOfAclrtLaunchKernelImpl inst;
    uint32_t aaa = 10;
    inst.profObj_ = std::make_shared<ProfDataCollect>(nullptr);
    inst.Pre(funcHandle_, 3, &aaa, 4, stream);
    EXPECT_TRUE(inst.Post(ACL_SUCCESS) == ACL_SUCCESS);
    EXPECT_TRUE(inst.profObj_ != nullptr);
    GlobalMockObject::verify();
}

TEST_F(HijackedFuncOfAclrtLaunchKernelTest, test_operand_record_expand_args_failed)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    MOCKER(&ProfDataCollect::IsOperandRecordNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, FuncContextSP(*)(const LaunchContextSP &)).stubs().will(returnValue(false));
    uint8_t* testBuffer = nullptr;
    MOCKER(&InitMemory, uint8_t*(*)(uint64_t)).stubs().will(returnValue(testBuffer));
    MOCKER(&rtGetL2CacheOffsetOrigin).stubs().will(returnValue(ACL_SUCCESS));
    HijackedFuncOfAclrtLaunchKernelImpl inst;
    auto func = []() -> void {};
    inst.refreshParamFunc_ = func;
    aclrtStream stream = &placeholder_;
    uint32_t aaa = 10;
    inst.profObj_ = std::make_shared<ProfDataCollect>(nullptr);
    inst.Pre(funcHandle_, 3, &aaa, 4, stream);
    testing::internal::CaptureStdout();
    inst.ProfPost();
    string capture = testing::internal::GetCapturedStdout();
    ASSERT_TRUE(capture.find("ExpandArgs failed"));
    GlobalMockObject::verify();
}
