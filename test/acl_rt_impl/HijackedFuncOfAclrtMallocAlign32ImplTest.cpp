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
#define private public
#include "acl_rt_impl/HijackedFunc.h"
#include "acl_rt_impl/HijackedFunc.h"
#undef private
#include "core/DomainSocket.h"
#include "core/FuncSelector.h"
#include "core/LocalProcess.h"
#include "mockcpp/mockcpp.hpp"

TEST(HijackedFuncOfAclrtMallocAlign32Impl, sanitizer_acl_malloc_align32_expect_get_correct_params) {
    MOCKER(IsSanitizer).stubs().will(returnValue(true));

    void *devPtr = nullptr;
    size_t size = 10;
    aclrtMemMallocPolicy policy{};
    HijackedFuncOfAclrtMallocAlign32Impl instance;
    instance.Pre(&devPtr, size, policy);
    aclError error{};
    instance.Post(error);
    ASSERT_EQ(instance.devPtr_, &devPtr);
    ASSERT_EQ(instance.size_, size);
    GlobalMockObject::verify();
}

TEST(HijackedFuncOfAclrtMallocAlign32Impl, opprof_acl_malloc_align32_expect_get_correct_params) {
    MOCKER(IsOpProf).stubs().will(returnValue(true));

    void *devPtr = nullptr;
    size_t size = 10;
    aclrtMemMallocPolicy policy{};
    HijackedFuncOfAclrtMallocAlign32Impl instance;
    instance.Pre(&devPtr, size, policy);
    aclError error{};
    instance.Post(error);
    ASSERT_EQ(instance.devPtr_, &devPtr);
    ASSERT_EQ(instance.size_, size);
    GlobalMockObject::verify();
}
