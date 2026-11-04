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
#define protected public
#include "acl_rt_impl/HijackedFunc.h"
#undef private
#undef protected

#include "core/FuncSelector.h"
#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/ThreadContext.h"
#include "runtime/inject_helpers/LocalDevice.h"

TEST(AclrtDevicePeerAccessImpl, CapturesDirectionAndPreservesFailure) {
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    ThreadContext::Instance().SetDeviceId(2);
    MOCKER(aclrtGetDeviceImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));

    HijackedFuncOfAclrtDeviceEnablePeerAccessImpl enable;
    enable.Pre(3, 0);
    EXPECT_EQ(enable.deviceId_, 2);
    EXPECT_EQ(enable.peerDeviceId_, 3);
    EXPECT_EQ(enable.Post(ACL_ERROR_INTERNAL_ERROR), ACL_ERROR_INTERNAL_ERROR);

    HijackedFuncOfAclrtDeviceDisablePeerAccessImpl disable;
    disable.Pre(3);
    EXPECT_EQ(disable.deviceId_, 2);
    EXPECT_EQ(disable.peerDeviceId_, 3);
    EXPECT_EQ(disable.Post(ACL_ERROR_INTERNAL_ERROR), ACL_ERROR_INTERNAL_ERROR);
    // 成功回调各上报一次；失败回调不能发送授权变更
    LocalDevice::GetInstance(2, CommType::MEMORY);
    MOCKER(&LocalProcess::Notify).expects(exactly(2)).will(returnValue(ssize_t{1}));
    enable.originfunc_ = [](int32_t, uint32_t) { return ACL_SUCCESS; };
    EXPECT_EQ(enable.Call(3, 0), ACL_SUCCESS);
    disable.originfunc_ = [](int32_t) { return ACL_SUCCESS; };
    EXPECT_EQ(disable.Call(3), ACL_SUCCESS);
    GlobalMockObject::verify();
}

TEST(AclrtDevicePeerAccessImpl, MissingOriginReturnsErrorWithoutReporting) {
    MOCKER(&LocalProcess::Notify).expects(never());
    HijackedFuncOfAclrtDeviceEnablePeerAccessImpl enable;
    HijackedFuncOfAclrtDeviceDisablePeerAccessImpl disable;
    enable.originfunc_ = nullptr;
    disable.originfunc_ = nullptr;
    EXPECT_EQ(enable.Call(3, 0), ACL_ERROR_INTERNAL_ERROR);
    EXPECT_EQ(disable.Call(3), ACL_ERROR_INTERNAL_ERROR);
    GlobalMockObject::verify();
}
