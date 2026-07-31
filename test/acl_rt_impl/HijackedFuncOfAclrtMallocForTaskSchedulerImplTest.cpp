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

// 测试用例
class HijackedFuncOfAclrtMallocForTaskSchedulerImplTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 设置 Mock 行为
        MOCKER(aclrtGetDeviceImplOrigin).stubs().with(any()).will(returnValue(ACL_SUCCESS));
        MOCKER(&LocalDevice::Notify).stubs().with(any()).will(returnValue(1));
    }

    void TearDown() override { GlobalMockObject::verify(); }
};

TEST_F(HijackedFuncOfAclrtMallocForTaskSchedulerImplTest, TestPostWithSanitizerAndSuccess) {
    // 设置条件
    HijackedFuncOfAclrtMallocForTaskSchedulerImpl func;
    void *devPtr = nullptr;
    size_t size = 1024;
    aclrtMemMallocPolicy policy = ACL_MEM_MALLOC_HUGE_FIRST;
    func.Pre(&devPtr, size, policy, nullptr);

    // 模拟条件
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(aclrtGetDeviceImplOrigin).stubs().with(any()).will(returnValue(ACL_SUCCESS));

    // 执行
    aclError ret = func.Post(ACL_SUCCESS);

    // 验证
    EXPECT_EQ(ret, ACL_SUCCESS);
}

TEST_F(HijackedFuncOfAclrtMallocForTaskSchedulerImplTest, TestPostWithSanitizerAndNone) {
    // ACL_ERROR_NONE == ACL_SUCCESS == 0，测试传入零值时的行为
    HijackedFuncOfAclrtMallocForTaskSchedulerImpl func;
    void *devPtr = nullptr;
    size_t size = 1024;
    aclrtMemMallocPolicy policy = ACL_MEM_MALLOC_HUGE_FIRST;
    func.Pre(&devPtr, size, policy, nullptr);

    MOCKER(IsSanitizer).stubs().will(returnValue(true));

    aclError ret = func.Post(ACL_ERROR_NONE);

    EXPECT_EQ(ret, ACL_ERROR_NONE);
}

TEST_F(HijackedFuncOfAclrtMallocForTaskSchedulerImplTest, TestPostWithSanitizerAndError) {
    // 测试传入真正错误码时的 early return 路径
    HijackedFuncOfAclrtMallocForTaskSchedulerImpl func;
    void *devPtr = reinterpret_cast<void *>(0xDEAD);
    size_t size = 1024;
    aclrtMemMallocPolicy policy = ACL_MEM_MALLOC_HUGE_FIRST;
    func.Pre(&devPtr, size, policy, nullptr);

    MOCKER(IsSanitizer).stubs().will(returnValue(true));

    aclError ret = func.Post(ACL_ERROR_BAD_ALLOC);

    EXPECT_EQ(ret, ACL_ERROR_BAD_ALLOC);
}

TEST_F(HijackedFuncOfAclrtMallocForTaskSchedulerImplTest, TestPostWithOpProfAndSuccess) {
    // 设置条件
    HijackedFuncOfAclrtMallocForTaskSchedulerImpl func;
    void *devPtr = nullptr;
    size_t size = 1024;
    aclrtMemMallocPolicy policy = ACL_MEM_MALLOC_HUGE_FIRST;
    func.Pre(&devPtr, size, policy, nullptr);

    // 模拟条件
    MOCKER(IsOpProf).stubs().will(returnValue(true));
    ProfConfig::Instance().profConfig_.isSimulator = false;
    ProfConfig::Instance().isAppReplay_ = false;
    MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(false));

    // 执行
    aclError ret = func.Post(ACL_SUCCESS);

    // 验证
    EXPECT_EQ(ret, ACL_SUCCESS);
}

TEST_F(HijackedFuncOfAclrtMallocForTaskSchedulerImplTest, TestPostWithOpProfAndNone) {
    // ACL_ERROR_NONE == ACL_SUCCESS == 0，测试传入零值时的行为
    HijackedFuncOfAclrtMallocForTaskSchedulerImpl func;
    void *devPtr = nullptr;
    size_t size = 1024;
    aclrtMemMallocPolicy policy = ACL_MEM_MALLOC_HUGE_FIRST;
    func.Pre(&devPtr, size, policy, nullptr);

    MOCKER(IsOpProf).stubs().will(returnValue(true));
    ProfConfig::Instance().profConfig_.isSimulator = false;
    ProfConfig::Instance().isAppReplay_ = false;
    MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(false));

    aclError ret = func.Post(ACL_ERROR_NONE);

    EXPECT_EQ(ret, ACL_ERROR_NONE);
}

TEST_F(HijackedFuncOfAclrtMallocForTaskSchedulerImplTest, TestPostWithOpProfAndError) {
    // 测试传入真正错误码时的 early return 路径
    HijackedFuncOfAclrtMallocForTaskSchedulerImpl func;
    void *devPtr = reinterpret_cast<void *>(0xDEAD);
    size_t size = 1024;
    aclrtMemMallocPolicy policy = ACL_MEM_MALLOC_HUGE_FIRST;
    func.Pre(&devPtr, size, policy, nullptr);

    MOCKER(IsOpProf).stubs().will(returnValue(true));
    ProfConfig::Instance().profConfig_.isSimulator = false;
    ProfConfig::Instance().isAppReplay_ = false;
    MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(false));

    aclError ret = func.Post(ACL_ERROR_BAD_ALLOC);

    EXPECT_EQ(ret, ACL_ERROR_BAD_ALLOC);
}
