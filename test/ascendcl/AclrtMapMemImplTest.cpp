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
#include "acl_rt_impl/HijackedFunc.h"
#undef private
#include "core/FuncSelector.h"
#include "core/DomainSocket.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "mockcpp/mockcpp.hpp"

TEST(aclrtMapMemImpl, sanitizer_call_pre_function_with_map_mem_args_expect_save_correct_params)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));

    int stubObj;
    void *devPtr = &stubObj;
    HijackedFuncOfAclrtMapMemImpl instance;
    instance.Pre(devPtr, 64, 0, nullptr, 0);
    ASSERT_EQ(instance.virPtr_, devPtr);
    ASSERT_EQ(instance.size_, 64UL);
    GlobalMockObject::verify();
}

TEST(aclrtMapMemImpl, sanitizer_call_post_function_with_map_mem_args_expect_return)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(aclrtGetDeviceImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));

    int stubObj;
    void *devPtr = &stubObj;
    HijackedFuncOfAclrtMapMemImpl instance;
    instance.Pre(devPtr, 64, 0, nullptr, 0);
    ASSERT_EQ(instance.Post(ACL_SUCCESS), ACL_SUCCESS);
    GlobalMockObject::verify();
}
