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


#include "gtest/gtest.h"
#include "mockcpp/mockcpp.hpp"
#include "acl_rt_impl/AscendclImplOrigin.h"

class AscendclImplOriginTest : public testing::Test {
    void SetUp() override
    {
        AscendclImplOriginCtor();
    }
};

TEST_F(AscendclImplOriginTest, call_aclrt_reset_device_force_impl_origin_expect_return_success)
{
    ASSERT_EQ(aclrtResetDeviceForceImplOrigin(0), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_memset_impl_origin_expect_return_success)
{
    ASSERT_EQ(aclrtMemsetImplOrigin(nullptr, 0, 0, 0), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_create_binary_impl_origin_expect_return_success)
{
    ASSERT_NE(aclrtCreateBinaryImplOrigin(nullptr, 0), nullptr);
}

TEST_F(AscendclImplOriginTest, call_aclrt_binary_load_impl_origin_expect_return_success)
{
    aclrtBinary binary{};
    ASSERT_EQ(aclrtBinaryLoadImplOrigin(binary, nullptr), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_binary_unload_impl_origin_expect_return_success)
{
    aclrtBinHandle handle{};
    ASSERT_EQ(aclrtBinaryUnLoadImplOrigin(handle), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_destroy_binary_impl_origin_expect_return_success)
{
    aclrtBinary binary{};
    ASSERT_EQ(aclrtDestroyBinaryImplOrigin(binary), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_binary_get_function_by_entry_impl_origin_expect_return_success)
{
    aclrtBinHandle handle{};
    ASSERT_EQ(aclrtBinaryGetFunctionByEntryImplOrigin(handle, 0, nullptr), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_kernel_args_init_by_user_mem_impl_origin_expect_return_success)
{
    aclrtFuncHandle func{};
    aclrtArgsHandle args{};
    ASSERT_EQ(aclrtKernelArgsInitByUserMemImplOrigin(func, args, nullptr, 0), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_kernel_args_get_mem_size_impl_origin_expect_return_success)
{
    aclrtFuncHandle func{};
    ASSERT_EQ(aclrtKernelArgsGetMemSizeImplOrigin(func, 0, nullptr), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_kernel_args_get_handle_mem_size_impl_origin_expect_return_success)
{
    aclrtFuncHandle func{};
    ASSERT_EQ(aclrtKernelArgsGetHandleMemSizeImplOrigin(func, nullptr), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_kernel_args_append_place_holder_impl_origin_expect_return_success)
{
    aclrtArgsHandle args{};
    ASSERT_EQ(aclrtKernelArgsAppendPlaceHolderImplOrigin(args, nullptr), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_kernel_args_get_place_holder_buffer_impl_origin_expect_return_success)
{
    aclrtArgsHandle args{};
    aclrtParamHandle param{};
    ASSERT_EQ(aclrtKernelArgsGetPlaceHolderBufferImplOrigin(args, param, 0, nullptr),
              ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_kernel_args_para_update_impl_origin_expect_return_success)
{
    aclrtArgsHandle args{};
    aclrtParamHandle param{};
    ASSERT_EQ(aclrtKernelArgsParaUpdateImplOrigin(args, param, nullptr, 0), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_launch_kernel_impl_origin_expect_return_success)
{
    aclrtFuncHandle func{};
    aclrtStream stream{};
    ASSERT_EQ(aclrtLaunchKernelImplOrigin(func, 0, nullptr, 0, stream), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_launch_kernel_v2_impl_origin_expect_return_success)
{
    aclrtFuncHandle func{};
    aclrtStream stream{};
    ASSERT_EQ(aclrtLaunchKernelV2ImplOrigin(func, 0, nullptr, 0, nullptr, stream), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclmdl_ri_capture_begin_impl_origin_expect_return_success)
{
    aclrtStream stream{};
    ASSERT_EQ(aclmdlRICaptureBeginImplOrigin(stream, ACL_MODEL_RI_CAPTURE_MODE_GLOBAL),
              ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclmdl_ri_capture_end_impl_origin_expect_return_success)
{
    aclrtStream stream{};
    ASSERT_EQ(aclmdlRICaptureEndImplOrigin(stream, nullptr), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_memcpy_async_impl_origin_expect_return_success)
{
    aclrtStream stream{};
    ASSERT_EQ(aclrtMemcpyAsyncImplOrigin(nullptr, 0, nullptr, 0, ACL_MEMCPY_HOST_TO_HOST, stream),
              ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_malloc_host_impl_origin_expect_return_success)
{
    ASSERT_EQ(aclrtMallocHostImplOrigin(nullptr, 0), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_free_host_impl_origin_expect_return_success)
{
    ASSERT_EQ(aclrtFreeHostImplOrigin(nullptr), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_stream_abort_impl_origin_expect_return_success) {
    aclrtStream stream{};
    ASSERT_EQ(aclrtStreamAbortImplOrigin(stream), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_launch_kernel_with_host_args_impl_origin_expect_return_success)
{
    ASSERT_EQ(aclrtLaunchKernelWithHostArgsImplOrigin(
              nullptr, 0, nullptr, nullptr, nullptr, 0, nullptr, 0), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_binary_load_from_data_impl_origin_expect_return_success)
{
    ASSERT_EQ(aclrtBinaryLoadFromDataImplOrigin(nullptr, 0, nullptr, nullptr), ACL_SUCCESS);
}

TEST_F(AscendclImplOriginTest, call_aclrt_binary_load_from_data_impl_return_success)
{
    int64_t value;
    ASSERT_EQ(aclrtGetFunctionAttributeImplOrigin(nullptr, aclrtFuncAttribute::ACL_FUNC_ATTR_KERNEL_TYPE, &value),
              ACL_SUCCESS);
}
