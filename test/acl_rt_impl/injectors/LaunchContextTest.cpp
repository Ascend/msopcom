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

#include "acl_rt_impl/HijackedFunc.h"
#include "runtime/inject_helpers/FuncManager.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <vector>

#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/LaunchContext.h"
#include "ContextMockHelper.h"

using namespace std;

class LaunchContextTest : public ContextMockHelper {
};

TEST_F(LaunchContextTest, mock_valid_input_then_test_get_context_method_expect_success)
{
    aclrtStream stream = &placeholder_;
    LaunchParam param{ 1, stream, true, 1};
    auto launchCtx = make_shared<LaunchContext>(tilingFuncCtx_, argsBinCtx_, param);
    ASSERT_NE(launchCtx, nullptr);
    ASSERT_EQ(launchCtx->GetFuncContext(), tilingFuncCtx_);
    ASSERT_EQ(launchCtx->GetArgsContext(), argsBinCtx_);
    ASSERT_EQ(launchCtx->GetLaunchId(), param.launchId);
    ASSERT_EQ(launchCtx->IsSink(), param.isSink);
}

class CpuFunctionRegistrationTest : public ContextMockHelper {};

TEST_F(CpuFunctionRegistrationTest, failed_context_creation_keeps_original_result_without_null_deref) {
    MOCKER_CPP(&FuncManager::CreateContext, FuncContextSP(FuncManager::*)(void *, const char *, void *))
        .stubs()
        .will(returnValue(FuncContextSP()));
    aclrtFuncHandle result = funcHandle_;
    HijackedFuncOfAclrtRegisterCpuFuncImpl hook;
    hook.Pre(binHandle_, "cpu_entry", "cpu_kernel", &result);
    EXPECT_EQ(hook.Post(ACL_SUCCESS), ACL_SUCCESS);
    EXPECT_FALSE(tilingFuncCtx_->isAiCpu);
}
