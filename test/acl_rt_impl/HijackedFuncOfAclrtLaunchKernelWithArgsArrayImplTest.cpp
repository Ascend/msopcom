/* -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
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

class HijackedFuncOfAclrtLaunchKernelWithArgsArrayImplTest : public ContextMockHelper {};

static aclError MockGetParamCount(const void *func, size_t *paramCount) {
    *paramCount = 1;
    return ACL_SUCCESS;
}

static aclError MockGetParamInfo(const void *func, size_t paramIndex, size_t *paramOffset, size_t *paramSize) {
    *paramOffset = 0;
    *paramSize = sizeof(uintptr_t);
    return ACL_SUCCESS;
}

static void MockQueryParamInfo() {
    MOCKER(&aclrtFunctionGetParamCountImplOrigin).stubs().will(invoke(MockGetParamCount));
    MOCKER(&aclrtFunctionGetParamInfoImplOrigin).stubs().will(invoke(MockGetParamInfo));
}

/**
 * |  用例集  | HijackedFuncOfAclrtLaunchKernelWithArgsArrayImplTest
 * | 测试函数 | Call
 * |  用例名  | mock_valid_hijacked_input_then_test_call_expect_ok
 * | 用例描述 | 工具类型为SANITIZER时，传入合法func/numBlocks/stream/cfg/args数组，
 * |          | 校验ArgsArray劫持Call能正确归一化func并完成原始launch，返回ACL_SUCCESS
 */
TEST_F(HijackedFuncOfAclrtLaunchKernelWithArgsArrayImplTest, mock_valid_hijacked_input_then_test_call_expect_ok) {
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    MockQueryParamInfo();
    aclrtStream stream = &placeholder_;
    aclrtLaunchKernelCfg cfg{};
    HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl inst;
    inst.originfunc_ = [](void *func, uint32_t numBlocks, aclrtStream stream, aclrtLaunchKernelCfg *cfg, void **args) {
        return ACL_SUCCESS;
    };
    uint32_t aaa = 10;
    void *args[] = {&aaa};
    ASSERT_EQ(inst.Call(funcHandle_, 3, stream, &cfg, args), ACL_SUCCESS);
    GlobalMockObject::verify();
}

/**
 * |  用例集  | HijackedFuncOfAclrtLaunchKernelWithArgsArrayImplTest
 * | 测试函数 | Call
 * |  用例名  | input_nullptr_then_test_call_expect_no_core_dump
 * | 用例描述 | 工具类型为SANITIZER时，func/numBlocks/stream/cfg/args入参全部为空，
 * |          | 校验ArgsArray劫持Call在空入参场景下不崩溃
 */
TEST_F(HijackedFuncOfAclrtLaunchKernelWithArgsArrayImplTest, input_nullptr_then_test_call_expect_no_core_dump) {
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl inst;
    (void)inst.Call(nullptr, 0, nullptr, nullptr, nullptr);
}

/**
 * |  用例集  | HijackedFuncOfAclrtLaunchKernelWithArgsArrayImplTest
 * | 测试函数 | Pre / Post
 * |  用例名  | call_function_msprof_simulator_init
 * | 用例描述 | 工具类型为PROF且isSimulator=true时，校验ArgsArray劫持走仿真prof采集分支，
 * |          | Pre/Post正常执行且返回ACL_SUCCESS
 */
TEST_F(HijackedFuncOfAclrtLaunchKernelWithArgsArrayImplTest, call_function_msprof_simulator_init) {
    FuncSelector::Instance().Set(ToolType::PROF);
    std::string r;
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(r));
    ProfConfig::Instance().profConfig_.isSimulator = true;
    HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl inst;
    inst.profObj_ = std::make_shared<ProfDataCollect>(nullptr);
    inst.Pre(nullptr, 0, nullptr, nullptr, nullptr);
    EXPECT_TRUE(inst.Post(ACL_SUCCESS) == ACL_SUCCESS);
    GlobalMockObject::verify();
}

/**
 * |  用例集  | HijackedFuncOfAclrtLaunchKernelWithArgsArrayImplTest
 * | 测试函数 | Pre / Post
 * |  用例名  | prof_gen_bbfile_and_dbifile_fail
 * | 用例描述 | 工具类型为PROF且isSimulator=false时，mock BB Count与Memory Chart需要生成但
 * |          | RunDBITask失败，校验ArgsArray劫持prof采集不崩溃，返回ACL_SUCCESS
 */
TEST_F(HijackedFuncOfAclrtLaunchKernelWithArgsArrayImplTest, prof_gen_bbfile_and_dbifile_fail) {
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    MockQueryParamInfo();
    MOCKER(&ProfDataCollect::IsBBCountNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsMemoryChartNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, FuncContextSP(*)(const LaunchContextSP &)).stubs().will(returnValue(false));
    aclrtStream stream = &placeholder_;
    HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl inst;
    aclrtLaunchKernelCfg cfg{};
    uint32_t aaa = 10;
    void *args[] = {&aaa};
    inst.profObj_ = std::make_shared<ProfDataCollect>(nullptr);
    inst.Pre(funcHandle_, 3, stream, &cfg, args);
    EXPECT_TRUE(inst.Post(ACL_SUCCESS) == ACL_SUCCESS);
    GlobalMockObject::verify();
}

/**
 * |  用例集  | HijackedFuncOfAclrtLaunchKernelWithArgsArrayImplTest
 * | 测试函数 | ProfPost
 * |  用例名  | test_operand_record_expand_args_failed
 * | 用例描述 | 工具类型为PROF时，mock OperandRecord需要生成但InitMemory返回空，
 * |          | 校验ArgsArray劫持OperandRecord插桩在ExpandArgs失败路径下打印错误日志且不崩溃
 */
TEST_F(HijackedFuncOfAclrtLaunchKernelWithArgsArrayImplTest, test_operand_record_expand_args_failed) {
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    MockQueryParamInfo();
    MOCKER(&ProfDataCollect::IsOperandRecordNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, FuncContextSP(*)(const LaunchContextSP &)).stubs().will(returnValue(false));
    uint8_t *testBuffer = nullptr;
    MOCKER(&InitMemory, uint8_t * (*)(uint64_t)).stubs().will(returnValue(testBuffer));
    MOCKER(&rtGetL2CacheOffsetOrigin).stubs().will(returnValue(ACL_SUCCESS));
    HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl inst;
    aclrtLaunchKernelCfg cfg{};
    uint32_t aaa = 10;
    void *args[] = {&aaa};
    auto func = []() -> void {};
    inst.refreshParamFunc_ = func;
    aclrtStream stream = &placeholder_;
    inst.profObj_ = std::make_shared<ProfDataCollect>(nullptr);
    inst.Pre(funcHandle_, 3, stream, &cfg, args);
    testing::internal::CaptureStdout();
    inst.ProfPost();
    string capture = testing::internal::GetCapturedStdout();
    ASSERT_TRUE(capture.find("ExpandArgs failed"));
    GlobalMockObject::verify();
}
