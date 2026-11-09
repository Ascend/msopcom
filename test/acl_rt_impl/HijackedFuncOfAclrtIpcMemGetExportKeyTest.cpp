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
#include "runtime/inject_helpers/MemGuard.h"

class HijackedFuncOfAclrtIpcMemGetExportKey : public testing::Test {
    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(HijackedFuncOfAclrtIpcMemGetExportKey, post_not_sanitizer_expect_return_success)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    HijackedFuncOfAclrtIpcMemGetExportKeyImpl instance;
    FuncSelector::Instance().Set(ToolType::TEST);

    ASSERT_EQ(instance.Post(ACL_ERROR_NONE), ACL_ERROR_NONE);
}

TEST_F(HijackedFuncOfAclrtIpcMemGetExportKey, post_key_nullptr_expect_return_success)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    HijackedFuncOfAclrtIpcMemGetExportKeyImpl instance;
    FuncSelector::Instance().Set(ToolType::SANITIZER);

    ASSERT_EQ(instance.Post(ACL_ERROR_NONE), ACL_ERROR_NONE);
}

TEST_F(HijackedFuncOfAclrtIpcMemGetExportKey, origin_func_call_success_expect_return_success)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    HijackedFuncOfAclrtIpcMemGetExportKeyImpl instance;
    FuncSelector::Instance().Set(ToolType::SANITIZER);

    void *ptr = reinterpret_cast<void*>(0x12005810000);
    uint64_t size = 1024;
    constexpr uint32_t len = 65;
    char key[len + 1];

    ASSERT_EQ(instance.Call(ptr, size, key, len, 0), ACL_ERROR_INTERNAL_ERROR);

    auto func = [](void *devPtr, size_t size, char *key, size_t len, uint64_t flag) -> aclError {
        return ACL_ERROR_NONE;
    };
    instance.originfunc_ = func;
    ASSERT_EQ(instance.Call(ptr, size, key, len, 0), ACL_ERROR_NONE);
}

TEST_F(HijackedFuncOfAclrtIpcMemGetExportKey, origin_func_call_failed_expect_return_failed)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    HijackedFuncOfAclrtIpcMemGetExportKeyImpl instance;
    FuncSelector::Instance().Set(ToolType::SANITIZER);

    void *ptr = reinterpret_cast<void*>(0x12005810000);
    uint64_t size = 1024;
    constexpr uint32_t len = 65;
    char key[len + 1];

    ASSERT_EQ(instance.Call(ptr, size, key, len, 0), ACL_ERROR_INTERNAL_ERROR);

    auto func = [](void *devPtr, size_t size, char *key, size_t len, uint64_t flag) -> aclError {
        return ACL_ERROR_INTERNAL_ERROR;
    };
    instance.originfunc_ = func;
    ASSERT_EQ(instance.Call(ptr, size, key, len, 0), ACL_ERROR_INTERNAL_ERROR);
}

// 测试场景：保护区使能，但HCCL导出地址不在工具分配表中
// 测试预期：原样导出4MiB，不额外增加32字节
TEST_F(HijackedFuncOfAclrtIpcMemGetExportKey, untracked_export_keeps_original_range)
{
    auto &guard = MemoryGuard::Instance();
    bool previous = guard.memGuardEnable_;
    guard.memGuardEnable_ = true;
    MOCKER(&MemoryGuard::GetGuardBlockInfoByUserAddr).stubs().will(returnValue(false));
    HijackedFuncOfAclrtIpcMemGetExportKeyImpl instance;
    instance.originfunc_ = [](void *ptr, size_t size, char *, size_t, uint64_t) -> aclError {
        EXPECT_EQ(ptr, reinterpret_cast<void *>(0x100000));
        EXPECT_EQ(size, 0x400000U);
        return ACL_ERROR_INTERNAL_ERROR;
    };
    EXPECT_EQ(instance.Call(reinterpret_cast<void *>(0x100000), 0x400000, nullptr, 0, 0),
              ACL_ERROR_INTERNAL_ERROR);
    guard.memGuardEnable_ = previous;
}

// 测试场景：工具托管分配分别完整导出、前缀导出和内部子区间导出
// 测试预期：完整导出含实际32字节后保护区；两个子区间保持原始范围
TEST_F(HijackedFuncOfAclrtIpcMemGetExportKey, guarded_full_and_partial_export_ranges)
{
    auto &guard = MemoryGuard::Instance();
    bool previous = guard.memGuardEnable_;
    size_t front = 0;
    size_t back = 0;
    guard.GetGuardSizes(front, back);
    guard.memGuardEnable_ = true;
    guard.SetGuardSizes(0, 32);
    void *ptr = reinterpret_cast<void *>(0x200000);
    guard.MallocProc(&ptr, 1024);
    HijackedFuncOfAclrtIpcMemGetExportKeyImpl instance;
    instance.originfunc_ = [](void *addr, size_t size, char *, size_t, uint64_t flag) -> aclError {
        EXPECT_EQ(addr, reinterpret_cast<void *>(flag == 2 ? 0x200080 : 0x200000));
        EXPECT_EQ(size, flag == 0 ? 1056U : 512U);
        return ACL_ERROR_INTERNAL_ERROR;
    };
    EXPECT_EQ(instance.Call(ptr, 1024, nullptr, 0, 0), ACL_ERROR_INTERNAL_ERROR);
    EXPECT_EQ(instance.Call(ptr, 512, nullptr, 0, 1), ACL_ERROR_INTERNAL_ERROR);
    EXPECT_EQ(instance.Call(reinterpret_cast<char *>(ptr) + 128, 512, nullptr, 0, 2), ACL_ERROR_INTERNAL_ERROR);
    guard.FreeProc(ptr);
    guard.SetGuardSizes(front, back);
    guard.memGuardEnable_ = previous;
}
