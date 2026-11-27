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
#define protected public
#include "acl_rt_impl/HijackedFunc.h"
#include "runtime/inject_helpers/ProfConfig.h"
#undef private
#undef protected
#include "core/DomainSocket.h"
#include "core/FuncSelector.h"
#include "core/LocalProcess.h"
#include "mockcpp/mockcpp.hpp"
#include "runtime/RuntimeOrigin.h"
#include "runtime/inject_helpers/KernelContext.h"

TEST(aclrtSetDeviceImpl, sanitizer_call_pre_function_with_devid_expect_save_correct_devid)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(IsOpProf).stubs().will(returnValue(false));
    std::string socVersion = "Ascend950PR_9589";
    MOCKER(aclrtGetSocNameImplOrigin).stubs().will(returnValue(socVersion.c_str()));
    MOCKER(rtDeviceSetLimitOrigin).stubs().will(returnValue(RT_ERROR_NONE));

    int32_t devId = 123;
    HijackedFuncOfAclrtSetDeviceImpl instance;
    instance.Pre(devId);
    ASSERT_EQ(instance.devId_, devId);
    instance.Call(devId);
    GlobalMockObject::verify();
}

TEST(aclrtSetDeviceImpl, sanitizer_call_post_function_with_get_soc_version_success_expect_return)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(IsOpProf).stubs().will(returnValue(false));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(rtGetDeviceOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    MOCKER(&rtGetSocVersionOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    std::string socVersion = "Ascend910_93";
    MOCKER(aclrtGetSocNameImplOrigin).stubs().will(returnValue(socVersion.c_str()));
    MOCKER(rtDeviceSetLimitOrigin).stubs().will(returnValue(RT_ERROR_NONE));

    int32_t devId = 123;
    HijackedFuncOfAclrtSetDeviceImpl instance;
    instance.Pre(devId);
    ASSERT_EQ(instance.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(aclrtSetDeviceImpl, sanitizer_call_post_function_with_get_soc_version_failed_expect_return)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(IsOpProf).stubs().will(returnValue(false));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(rtGetDeviceOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    MOCKER(&rtGetSocVersionOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    std::string socVersion = "Ascend910_93";
    MOCKER(aclrtGetSocNameImplOrigin).stubs().will(returnValue(socVersion.c_str()));
    MOCKER(rtDeviceSetLimitOrigin).stubs().will(returnValue(RT_ERROR_NONE));

    int32_t devId = 123;
    HijackedFuncOfAclrtSetDeviceImpl instance;
    instance.Pre(devId);
    ASSERT_EQ(instance.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(aclrtSetDeviceImpl, opprof_call_with_originfunc)
{
    MOCKER(IsOpProf).stubs().will(returnValue(true));
    MOCKER(IsSanitizer).stubs().will(returnValue(false));
    ProfConfig::Instance().profConfig_.isSimulator = true;

    int32_t devId = 123;

    HijackedFuncOfAclrtSetDeviceImpl instance;

    auto func = [](int32_t) -> aclError { return ACL_SUCCESS; };
    instance.originfunc_ = func;

    instance.Pre(devId);
    ASSERT_EQ(instance.devId_, devId);
    instance.Call(devId);
    GlobalMockObject::verify();
}
