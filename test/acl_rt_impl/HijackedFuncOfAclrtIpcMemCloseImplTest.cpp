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

#include <string>
#include <algorithm>
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
#include "runtime/inject_helpers/IPCMemManager.h"
using namespace std;

// 测试用例
class HijackedFuncOfAclrtIpcMemCloseImplTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(HijackedFuncOfAclrtIpcMemCloseImplTest, TestPostWithSanitizerAndKeyIsNull)
{
    // 设置条件
    HijackedFuncOfAclrtIpcMemCloseImpl func;
    func.Pre(nullptr);
    // 模拟条件
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    // 执行
    aclError ret = func.Post(ACL_SUCCESS);
    // 验证
    EXPECT_EQ(ret, ACL_SUCCESS);
    GlobalMockObject::verify();
}

TEST_F(HijackedFuncOfAclrtIpcMemCloseImplTest, TestPostWithSanitizer)
{
    // 设置条件
    HijackedFuncOfAclrtIpcMemCloseImpl func;
    func.Pre(nullptr);
    // 模拟条件
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    // 执行
    aclError ret = func.Post(ACL_SUCCESS);
    // 验证
    EXPECT_EQ(ret, ACL_SUCCESS);
    GlobalMockObject::verify();
}

TEST_F(HijackedFuncOfAclrtIpcMemCloseImplTest, TestPostWithSanitizerAndKeyNotFound)
{
    // 设置条件
    HijackedFuncOfAclrtIpcMemCloseImpl func;
    const char* key = "test_key";
    const char* keyInsert = "test_key_insert";
    func.Pre(key);
    // 模拟条件
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    IPCMemManager::IPCMemInfo ipcMemInfo{IPCMemManager::IPCMemActor::SHAREE, nullptr};
    IPCMemManager::Instance().ipcMemInfoMap.insert({keyInsert, ipcMemInfo});
    // 执行
    aclError ret = func.Post(ACL_ERROR_NONE);
    // 验证
    EXPECT_EQ(ret, ACL_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST_F(HijackedFuncOfAclrtIpcMemCloseImplTest, TestPostWithSanitizerAndKeyFoundAsSharer)
{
    // 设置条件
    HijackedFuncOfAclrtIpcMemCloseImpl func;
    const char* key = "test_key";
    func.Pre(key);

    // 模拟条件
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    IPCMemManager::IPCMemInfo ipcMemInfo{IPCMemManager::IPCMemActor::SHARER, nullptr};
    IPCMemManager::Instance().ipcMemInfoMap.insert({key, ipcMemInfo});

    // 执行
    aclError ret = func.Post(ACL_SUCCESS);

    // 验证
    EXPECT_EQ(ret, ACL_SUCCESS);
    GlobalMockObject::verify();
}

TEST_F(HijackedFuncOfAclrtIpcMemCloseImplTest, TestPostWithSanitizerAndKeyFoundAsSharee)
{
    // 设置条件
    HijackedFuncOfAclrtIpcMemCloseImpl func;
    const char* key = "test_key";
    func.Pre(key);

    // 模拟条件
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    IPCMemManager::IPCMemInfo ipcMemInfo{IPCMemManager::IPCMemActor::SHAREE, nullptr};
    IPCMemManager::Instance().ipcMemInfoMap.insert({key, ipcMemInfo});

    // 执行
    aclError ret = func.Post(ACL_SUCCESS);

    // 验证
    EXPECT_EQ(ret, ACL_SUCCESS);
    GlobalMockObject::verify();
}
// 测试场景：真实IPC Close失败，原导入记录仍有效
// 测试预期：保留记录，不发送UNMAP或DESTROY
TEST_F(HijackedFuncOfAclrtIpcMemCloseImplTest, FailedClosePreservesIPCRecord)
{
    const char *key = "hccl_failed_close";
    auto &records = IPCMemManager::Instance().ipcMemInfoMap;
    records[key] = {IPCMemManager::IPCMemActor::SHAREE, reinterpret_cast<void *>(0x123400)};
    HijackedFuncOfAclrtIpcMemCloseImpl func;
    func.Pre(key);
    EXPECT_EQ(func.Post(ACL_ERROR_INTERNAL_ERROR), ACL_ERROR_INTERNAL_ERROR);
    ASSERT_EQ(records.count(key), 1U);
    EXPECT_EQ(records.at(key).devPtr, reinterpret_cast<void *>(0x123400));
    records.erase(key);
    GlobalMockObject::verify();
}
