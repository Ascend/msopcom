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

#define private public
#define protected public
#include "acl_rt_impl/HijackedFunc.h"
#undef private
#undef protected

#include "core/FuncSelector.h"
#include "core/DomainSocket.h"
#include "runtime/inject_helpers/LocalDevice.h"

class HijackedFuncOfAclrtIpcMemImportByKey : public testing::Test {
    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(HijackedFuncOfAclrtIpcMemImportByKey, post_not_sanitizer_expect_return_ret)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    HijackedFuncOfAclrtIpcMemImportByKeyImpl instance;
    FuncSelector::Instance().Set(ToolType::TEST);

    ASSERT_EQ(instance.Post(ACL_ERROR_NONE), ACL_ERROR_NONE);
}

TEST_F(HijackedFuncOfAclrtIpcMemImportByKey, post_key_nullptr_expect_return_ret)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    HijackedFuncOfAclrtIpcMemImportByKeyImpl instance;
    FuncSelector::Instance().Set(ToolType::SANITIZER);

    ASSERT_EQ(instance.Post(ACL_ERROR_NONE), ACL_ERROR_NONE);
}

TEST_F(HijackedFuncOfAclrtIpcMemImportByKey, post_devptr_nullptr_expect_return_ret)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    HijackedFuncOfAclrtIpcMemImportByKeyImpl instance;
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    instance.key_ = "IPC_MEM_NAME_01234";
    ASSERT_EQ(instance.Post(ACL_ERROR_NONE), ACL_ERROR_NONE);
}

TEST_F(HijackedFuncOfAclrtIpcMemImportByKey, origin_func_call_success_expect_return_success)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    HijackedFuncOfAclrtIpcMemImportByKeyImpl instance;
    FuncSelector::Instance().Set(ToolType::SANITIZER);

    void *ptr{};
    const char *key = "IPC_MEM_NAME_01234";

    ASSERT_EQ(instance.Call(&ptr, key, 0), ACL_ERROR_INTERNAL_ERROR);

    auto func = [](void **devPtr, const char *key, uint64_t flag) -> aclError {
        return ACL_ERROR_NONE;
    };
    instance.originfunc_ = func;
    ASSERT_EQ(instance.Call(&ptr, key, 0), ACL_ERROR_NONE);
}

TEST_F(HijackedFuncOfAclrtIpcMemImportByKey, origin_func_call_failed_expect_return_failed)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    HijackedFuncOfAclrtIpcMemImportByKeyImpl instance;
    FuncSelector::Instance().Set(ToolType::SANITIZER);

    void *ptr{};
    const char *key = "IPC_MEM_NAME_01234";

    ASSERT_EQ(instance.Call(&ptr, key, 0), ACL_ERROR_INTERNAL_ERROR);

    auto func = [](void **devPtr, const char *key, uint64_t flag) -> aclError {
        return ACL_ERROR_INTERNAL_ERROR;
    };
    instance.originfunc_ = func;
    ASSERT_EQ(instance.Call(&ptr, key, 0), ACL_ERROR_INTERNAL_ERROR);
}
