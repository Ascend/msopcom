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

#include "acl_rt_impl/AscendclImplOrigin.h"
#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/ArgsHandleContext.h"
using namespace std;

namespace {

uint64_t g_argsHandleStorage{};

aclError aclrtKernelArgsInitImplOriginStub(aclrtFuncHandle funcHandle, aclrtArgsHandle *argsHandle) {
    (void)funcHandle;
    *argsHandle = reinterpret_cast<aclrtArgsHandle>(&g_argsHandleStorage);
    return ACL_ERROR_NONE;
}

} // namespace [Dummy]

class ArgsHandleContextTest : public testing::Test {
public:
    void SetUp() override
    {
        ctx_ = make_shared<ArgsHandleContext>(&placeholder_, &placeholder_);
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
    uint64_t placeholder_;
    ArgsHandleContextSP ctx_;
};

TEST_F(ArgsHandleContextTest, input_valid_then_cache_args_append_op_expect_size_correct)
{
    uint64_t devAddr{};
    aclrtParamHandle paramHandle = &placeholder_;
    ctx_->CacheArgsAppendOp(&devAddr, sizeof(devAddr), paramHandle);
    ASSERT_EQ(ctx_->GetParams().size(), 1);
}

TEST_F(ArgsHandleContextTest, input_valid_then_cache_args_append_placeholder_op_expect_size_correct)
{
    aclrtParamHandle paramHandle = &placeholder_;
    ctx_->CacheArgsAppendPlaceholderOp(paramHandle);
    ASSERT_EQ(ctx_->GetParams().size(), 1);
}

TEST_F(ArgsHandleContextTest, input_valid_then_cache_args_get_placeholder_buffer_op_expect_size_correct)
{
    aclrtParamHandle paramHandle = &placeholder_;
    ctx_->CacheArgsAppendPlaceholderOp(paramHandle);
    ASSERT_EQ(ctx_->GetParams().size(), 1);
    std::vector<uint8_t> data(10, 1);
    ctx_->CacheArgsGetPlaceholderBufferOp(paramHandle, data.size(), data.data());
    ASSERT_EQ(ctx_->GetParams().size(), 1);
}

TEST_F(ArgsHandleContextTest, input_valid_then_cache_args_para_update_expect_size_correct)
{
    aclrtParamHandle paramHandle = &placeholder_;
    uint64_t devAddr{};
    ctx_->CacheArgsAppendOp(&devAddr, sizeof(devAddr), paramHandle);
    std::vector<uint8_t> data(10, 1);
    ctx_->CacheArgsParaUpdateOp(paramHandle, data.data(), data.size());
    ASSERT_EQ(ctx_->GetParams().size(), 1);
}

TEST_F(ArgsHandleContextTest, input_valid_then_finalize_expect_success)
{
    MOCKER(&aclrtKernelArgsInitImplOrigin).stubs().will(invoke(&aclrtKernelArgsInitImplOriginStub));
    MOCKER(&aclrtKernelArgsFinalizeImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&aclrtKernelArgsAppendImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));

    uint64_t devAddr{};
    vector<uint64_t> fakeParamH(5);
    vector<aclrtParamHandle> paramHandle = {
        &fakeParamH[0],
        &fakeParamH[1]
    };
    std::vector<uint8_t> data(10, 1);
    ctx_->CacheArgsAppendOp(&devAddr, sizeof(devAddr), paramHandle[0]);
    ctx_->CacheArgsAppendPlaceholderOp(paramHandle[1]);
    ctx_->CacheArgsGetPlaceholderBufferOp(paramHandle[1], data.size(), data.data());
    ctx_->CacheArgsParaUpdateOp(paramHandle[0], data.data(), data.size());
    ctx_->Finalize();
    ASSERT_TRUE(ctx_->IsFinalized());
    auto params = ctx_->GetParams();
    ASSERT_EQ(params[0].param.size(), 10);
    ASSERT_EQ(params[0].param[0], 1);
}

TEST_F(ArgsHandleContextTest, expand_args_expect_size_correct)
{
    uint64_t devAddr{};
    uint32_t paramOffset{};
    ctx_->ExpandArgs(&devAddr, sizeof(devAddr), paramOffset);
    ASSERT_EQ(ctx_->GetParams().size(), 1);
}

TEST_F(ArgsHandleContextTest, args_init_failed_expect_generate_args_handle_return_nullptr) {
    MOCKER(&aclrtKernelArgsInitImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));

    aclrtArgsHandle argsHandle = ctx_->GenerateArgsHandle();
    ASSERT_EQ(argsHandle, nullptr);
}

TEST_F(ArgsHandleContextTest, args_append_failed_expect_generate_args_handle_return_nullptr)
{
    MOCKER(&aclrtKernelArgsInitImplOrigin).stubs().will(invoke(&aclrtKernelArgsInitImplOriginStub));
    MOCKER(&aclrtKernelArgsAppendImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));

    uint64_t devAddr{};
    aclrtParamHandle paramHandle = &placeholder_;
    ctx_->CacheArgsAppendOp(&devAddr, sizeof(devAddr), paramHandle);
    aclrtArgsHandle argsHandle = ctx_->GenerateArgsHandle();
    ASSERT_EQ(argsHandle, nullptr);
}

TEST_F(ArgsHandleContextTest, args_append_placeholder_failed_expect_generate_args_handle_return_nullptr)
{
    MOCKER(&aclrtKernelArgsInitImplOrigin).stubs().will(invoke(&aclrtKernelArgsInitImplOriginStub));
    MOCKER(&aclrtKernelArgsAppendPlaceHolderImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));

    aclrtParamHandle paramHandle = &placeholder_;
    ctx_->CacheArgsAppendPlaceholderOp(paramHandle);
    aclrtArgsHandle argsHandle = ctx_->GenerateArgsHandle();
    ASSERT_EQ(argsHandle, nullptr);
}

TEST_F(ArgsHandleContextTest, args_get_placeholder_buffer_failed_expect_generate_args_handle_return_nullptr)
{
    MOCKER(&aclrtKernelArgsInitImplOrigin).stubs().will(invoke(&aclrtKernelArgsInitImplOriginStub));
    MOCKER(&aclrtKernelArgsAppendPlaceHolderImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&aclrtKernelArgsGetPlaceHolderBufferImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));

    aclrtParamHandle paramHandle = &placeholder_;
    ctx_->CacheArgsAppendPlaceholderOp(paramHandle);
    aclrtArgsHandle argsHandle = ctx_->GenerateArgsHandle();
    ASSERT_EQ(argsHandle, nullptr);
}

TEST_F(ArgsHandleContextTest, args_finalize_failed_expect_generate_args_handle_return_nullptr)
{
    MOCKER(&aclrtKernelArgsInitImplOrigin).stubs().will(invoke(&aclrtKernelArgsInitImplOriginStub));
    MOCKER(&aclrtKernelArgsAppendImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(aclrtKernelArgsFinalizeImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));

    aclrtArgsHandle argsHandle = ctx_->GenerateArgsHandle();
    ASSERT_EQ(argsHandle, nullptr);
}

TEST_F(ArgsHandleContextTest, args_append_all_args_success_expect_generate_args_handle_return_handle)
{
    MOCKER(&aclrtKernelArgsInitImplOrigin).stubs().will(invoke(&aclrtKernelArgsInitImplOriginStub));
    MOCKER(&aclrtKernelArgsAppendImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&aclrtKernelArgsAppendPlaceHolderImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&aclrtKernelArgsGetPlaceHolderBufferImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(aclrtKernelArgsFinalizeImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));

    uint64_t devAddr{};
    aclrtParamHandle paramHandle = &placeholder_;
    ctx_->CacheArgsAppendOp(&devAddr, sizeof(devAddr), paramHandle);
    ctx_->CacheArgsAppendPlaceholderOp(paramHandle);
    std::vector<uint8_t> data(10, 1);
    ctx_->CacheArgsGetPlaceholderBufferOp(paramHandle, data.size(), data.data());
    ctx_->Finalize();

    aclrtArgsHandle argsHandle = ctx_->GenerateArgsHandle();
    ASSERT_NE(argsHandle, nullptr);
}

TEST_F(ArgsHandleContextTest, UpdateNormalTaskArgsAddr_expect_return_false)
{
    OpMemInfo mem;
    DumperContext context;
    mem.inputParamsAddrInfos = { {1, 1, MemInfoSrc::EXTRA} };
    void *temp = nullptr;
    ctx_->CacheArgsAppendOp(temp, 0, temp);
    ASSERT_FALSE(ctx_->Save("", context, mem, false));
}
