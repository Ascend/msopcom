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
#include "acl_rt_impl/HijackedFunc.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "runtime/inject_helpers/StubFuncContext.h"
#include "runtime/inject_helpers/TilingFuncContext.h"
#include "runtime/inject_helpers/FuncManager.h"
#include "runtime/inject_helpers/RegisterManager.h"

using namespace std;

class FuncContextTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        AscendclImplOriginCtor();
    }

    void SetUp() override
    {
        auto sp = std::make_shared<RegisterContext>();
        string kernelName = "abc";
        MOCKER_CPP(&RegisterManager::GetContext).stubs().will(returnValue(sp));
        MOCKER_CPP(&RegisterContext::GetKernelName).stubs().will(returnValue(kernelName));
        uint64_t data{};
        void *hdl = &data;
        void *funcHandle = &data;
        tilingFuncCtx_ = FuncManager::Instance().CreateContext(hdl, uint64_t(0), funcHandle);
        stubFuncCtx_ = FuncManager::Instance().CreateContext(hdl, kernelName.c_str(), funcHandle);
        ASSERT_NE(stubFuncCtx_, nullptr);
        ASSERT_NE(tilingFuncCtx_, nullptr);
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        FuncManager::Instance().Clear();
    }

    FuncContextSP stubFuncCtx_;
    FuncContextSP tilingFuncCtx_;
};

namespace {
int64_t runtimeKernelType = 0;
aclError ReturnRuntimeKernelType(aclrtFuncHandle, aclrtFuncAttribute, int64_t *value) {
    *value = runtimeKernelType;
    return ACL_SUCCESS;
}

aclError ReturnTwoParamCount(const void *, size_t *count) {
    *count = 2;
    return ACL_SUCCESS;
}

aclError ReturnParamLayout(const void *, size_t index, size_t *offset, size_t *size) {
    *offset = index * 16;
    *size = index == 0 ? 8 : 4;
    return ACL_SUCCESS;
}
}

TEST_F(FuncContextTest, mixed_binary_uses_runtime_function_core_type) {
    MOCKER(&MagicToKernelType).stubs().will(returnValue(KernelType::MIX));
    MOCKER(aclrtGetFunctionAttributeImplOrigin).stubs().will(invoke(ReturnRuntimeKernelType));
    runtimeKernelType = static_cast<int64_t>(aclrtKernelType::ACL_KERNEL_TYPE_VECTOR);
    EXPECT_EQ(stubFuncCtx_->GetKernelType(), KernelType::AIVEC);
    runtimeKernelType = static_cast<int64_t>(aclrtKernelType::ACL_KERNEL_TYPE_CUBE);
    EXPECT_EQ(stubFuncCtx_->GetKernelType(), KernelType::AICUBE);
    runtimeKernelType = static_cast<int64_t>(aclrtKernelType::ACL_KERNEL_TYPE_AICPU);
    EXPECT_EQ(stubFuncCtx_->GetKernelType(), KernelType::AICPU);
}

TEST_F(FuncContextTest, function_parameter_layout_is_cached_for_args_array_launch) {
    MOCKER(aclrtFunctionGetParamCountImplOrigin).stubs().will(invoke(ReturnTwoParamCount));
    MOCKER(aclrtFunctionGetParamInfoImplOrigin).stubs().will(invoke(ReturnParamLayout));
    ASSERT_TRUE(stubFuncCtx_->QueryParamInfo());
    EXPECT_EQ(stubFuncCtx_->GetParamCount(), 2U);
    EXPECT_EQ(stubFuncCtx_->GetParamOffsets(), (std::vector<size_t>{0, 16}));
    EXPECT_EQ(stubFuncCtx_->GetParamSizes(), (std::vector<size_t>{8, 4}));
}

TEST_F(FuncContextTest, mock_kernel_addr_then_test_get_start_pc_expect_success)
{
    MOCKER_CPP(&RegisterContext::GetKernelOffsetByName).stubs().will(returnValue(1));
    uint64_t aicAddr = 123;
    MOCKER_CPP(&aclrtGetFunctionAddrImplOrigin).stubs().with(any(), outBoundP((void**)&aicAddr), any()).will(returnValue(ACL_SUCCESS));

    ASSERT_EQ(stubFuncCtx_->GetStartPC(), aicAddr - 1);
}

TEST_F(FuncContextTest, mock_kernel_addr_fail_then_test_get_start_pc_expect_success)
{
    MOCKER_CPP(&RegisterContext::GetKernelOffsetByName).stubs().will(returnValue(1));
    uint64_t aicAddr = 123;
    MOCKER_CPP(&aclrtGetFunctionAddrImplOrigin).stubs().with(any(), outBoundP((void**)&aicAddr), any()).will(returnValue(aclError(1)));

    ASSERT_EQ(stubFuncCtx_->GetStartPC(), 0);
}

TEST_F(FuncContextTest, mock_zero_kernel_addr_then_test_get_start_pc_expect_success)
{
    MOCKER_CPP(&RegisterContext::GetKernelOffsetByName).stubs().will(returnValue(1));
    MOCKER_CPP(&aclrtGetFunctionAddrImplOrigin).stubs().will(returnValue(ACL_SUCCESS));

    ASSERT_EQ(stubFuncCtx_->GetStartPC(), 0);
}
