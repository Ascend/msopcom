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
#include <dlfcn.h>
#define private public
#define protected public
#include "runtime/inject_helpers/ProfConfig.h"
#include "core/HijackedFuncTemplate.h"
#undef private
#undef protected
#include "mockcpp/mockcpp.hpp"
#include "runtime/HijackedFunc.h"
#include "core/DomainSocket.h"
#include "core/LocalProcess.h"
#include "core/FuncSelector.h"

TEST(rtGetDevice, call_pre_function_with_valid_device_id_expect_return)
{
    MOCKER(&dlopen).stubs().will(returnValue(nullptr));
    HijackedFuncOfGetDevice instance;
    int32_t devId;
    instance.Pre(&devId);
    instance.Call(&devId);
    GlobalMockObject::verify();
}

TEST(rtGetDevice, call_pre_function_when_prof_success)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    HijackedFuncOfGetDevice instance;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    int32_t cnt;
    auto ret = instance.Call(&cnt);
    ASSERT_EQ(ret, RT_ERROR_RESERVED);
    GlobalMockObject::verify();
}

TEST(rtGetDevice, call_function_normal)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    HijackedFuncOfGetDevice instance;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    int32_t cnt;
    auto func = [](int32_t *a) -> rtError_t { return RT_ERROR_NONE; };
    instance.originfunc_ = func;
    auto ret = instance.Call(&cnt);
    ASSERT_EQ(ret, RT_ERROR_NONE);

    FuncSelector::Instance().Set(ToolType::SANITIZER);
    ret = instance.Call(&cnt);
    ASSERT_EQ(ret, RT_ERROR_NONE);

    FuncSelector::Instance().Set(ToolType::SANITIZER);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    ret = instance.Call(&cnt);
    ASSERT_EQ(ret, RT_ERROR_NONE);

    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    ret = instance.Call(&cnt);
    ASSERT_EQ(ret, RT_ERROR_NONE);

    GlobalMockObject::verify();
}
