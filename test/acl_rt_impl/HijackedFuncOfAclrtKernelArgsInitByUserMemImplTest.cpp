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
#include "runtime/inject_helpers/ArgsManager.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "core/FuncSelector.h"
#include "core/DomainSocket.h"

// 测试用例
class HijackedFuncOfAclrtKernelArgsInitByUserMemImplTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(HijackedFuncOfAclrtKernelArgsInitByUserMemImplTest, TestPostWithSuccessAndValidHandles)
{
    // 设置条件
    HijackedFuncOfAclrtKernelArgsInitByUserMemImpl func;
    aclrtFuncHandle funcHandle = reinterpret_cast<aclrtFuncHandle>(1);
    aclrtArgsHandle argsHandle = reinterpret_cast<aclrtArgsHandle>(1);
    func.Pre(funcHandle, argsHandle, nullptr, 0);

    // 执行
    aclError ret = func.Post(ACL_SUCCESS);

    // 验证
    EXPECT_EQ(ret, ACL_SUCCESS);
}

TEST_F(HijackedFuncOfAclrtKernelArgsInitByUserMemImplTest, TestPostWithSuccessAndNullFuncHandle)
{
    // 设置条件
    HijackedFuncOfAclrtKernelArgsInitByUserMemImpl func;
    aclrtFuncHandle funcHandle = nullptr;
    aclrtArgsHandle argsHandle = reinterpret_cast<aclrtArgsHandle>(1);
    func.Pre(funcHandle, argsHandle, nullptr, 0);

    // 执行
    aclError ret = func.Post(ACL_SUCCESS);

    // 验证
    EXPECT_EQ(ret, ACL_SUCCESS);
}

TEST_F(HijackedFuncOfAclrtKernelArgsInitByUserMemImplTest, TestPostWithSuccessAndNullArgsHandle)
{
    // 设置条件
    HijackedFuncOfAclrtKernelArgsInitByUserMemImpl func;
    aclrtFuncHandle funcHandle = reinterpret_cast<aclrtFuncHandle>(1);
    aclrtArgsHandle argsHandle = nullptr;
    func.Pre(funcHandle, argsHandle, nullptr, 0);

    // 执行
    aclError ret = func.Post(ACL_SUCCESS);

    // 验证
    EXPECT_EQ(ret, ACL_SUCCESS);
}

TEST_F(HijackedFuncOfAclrtKernelArgsInitByUserMemImplTest, TestPostWithFailure)
{
    // 设置条件
    HijackedFuncOfAclrtKernelArgsInitByUserMemImpl func;
    aclrtFuncHandle funcHandle = reinterpret_cast<aclrtFuncHandle>(1);
    aclrtArgsHandle argsHandle = reinterpret_cast<aclrtArgsHandle>(1);
    func.Pre(funcHandle, argsHandle, nullptr, 0);

    // 执行
    aclError ret = func.Post(ACL_SUCCESS);

    // 验证
    EXPECT_EQ(ret, ACL_SUCCESS);
}

TEST_F(HijackedFuncOfAclrtKernelArgsInitByUserMemImplTest, TestPostWithCreateContextFailure)
{
    // 设置条件
    HijackedFuncOfAclrtKernelArgsInitByUserMemImpl func;
    aclrtFuncHandle funcHandle = reinterpret_cast<aclrtFuncHandle>(1);
    aclrtArgsHandle argsHandle = reinterpret_cast<aclrtArgsHandle>(1);
    func.Pre(funcHandle, argsHandle, nullptr, 0);
    ArgsContextSP s;
    // 模拟条件
    MOCKER_CPP(&ArgsManager::CreateContext, ArgsContextSP(ArgsManager::*)(aclrtFuncHandle, aclrtArgsHandle)).stubs().will(returnValue(s));

    // 执行
    aclError ret = func.Post(ACL_SUCCESS);

    // 验证
    EXPECT_EQ(ret, ACL_SUCCESS);
    GlobalMockObject::verify();
}
