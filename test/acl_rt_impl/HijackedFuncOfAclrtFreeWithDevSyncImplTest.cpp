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
#undef private
#include "core/DomainSocket.h"
#include "core/FuncSelector.h"
#include "core/LocalProcess.h"
#include "mockcpp/mockcpp.hpp"

TEST(HijackedFuncOfAclrtFreeWithDevSyncImpl, sanitizer_acl_free_expect_get_correct_params) {
    MOCKER(IsSanitizer).stubs().will(returnValue(true));

    void *devPtr = reinterpret_cast<void *>(0x1234);
    HijackedFuncOfAclrtFreeWithDevSyncImpl instance;
    instance.Pre(devPtr);
    ASSERT_EQ(instance.devPtr_, devPtr);
    GlobalMockObject::verify();
}

TEST(HijackedFuncOfAclrtFreeWithDevSyncImpl, opprof_acl_free_expect_get_correct_params) {
    MOCKER(IsOpProf).stubs().will(returnValue(true));

    void *devPtr = reinterpret_cast<void *>(0x1234);
    HijackedFuncOfAclrtFreeWithDevSyncImpl instance;
    instance.Pre(devPtr);
    ASSERT_EQ(instance.devPtr_, devPtr);
    GlobalMockObject::verify();
}
