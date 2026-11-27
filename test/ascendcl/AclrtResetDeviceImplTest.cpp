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
#include "core/FuncSelector.h"
#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"

TEST(aclrtResetDeviceImpl, call_pre_function_with_valid_device_id_expect_return)
{
    HijackedFuncOfAclrtResetDeviceImpl instance;
    instance.Pre(1);
    instance.Call(1);
}

TEST(aclrtResetDeviceImpl, IsSanitizer)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    HijackedFuncOfAclrtResetDeviceImpl instance;
    instance.Pre(1);
    instance.Call(1);
}

TEST(aclrtResetDeviceImpl, IsOpProf)
{
    MOCKER(IsOpProf).stubs().will(returnValue(true));
    ProfConfig::Instance().profConfig_.isSimulator = true;
    HijackedFuncOfAclrtResetDeviceImpl instance;
    auto func = [](int32_t) -> aclError { return ACL_SUCCESS; };
    MOCKER(rtDeviceResetForceOrigin).stubs().will(returnValue(0));
    instance.originfunc_ = func;
    instance.Pre(1);
    instance.Call(1);
}
