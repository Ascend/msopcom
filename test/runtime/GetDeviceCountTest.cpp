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
#include "runtime/HijackedFunc.h"
#include "runtime/inject_helpers/ProfConfig.h"
#undef private
#undef protected
#include "mockcpp/mockcpp.hpp"
#include "runtime/RuntimeOrigin.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "mockcpp/mockcpp.hpp"
#include "core/FuncSelector.h"
#include "runtime/RuntimeConfig.h"
#include "core/DomainSocket.h"
#include "core/LocalProcess.h"

TEST(rtGetDeviceCount, get_device_count_with_null_origin_func_return)
{
    HijackedFuncOfGetDeviceCount instance;
    auto ret = instance.Call(nullptr);
    ASSERT_EQ(ret, RT_ERROR_RESERVED);
    GlobalMockObject::verify();
}

TEST(rtGetDeviceCount, get_device_count_with_originfunc_return)
{
    HijackedFuncOfGetDeviceCount instance;
    auto func = [](int32_t*) ->
        rtError_t { return RT_ERROR_NONE; };
    instance.originfunc_ = func;
    auto ret = instance.Call(nullptr);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtGetDeviceCount, get_device_count_when_prof_success)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    HijackedFuncOfGetDeviceCount instance;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    int32_t cnt;
    auto ret = instance.Call(&cnt);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}
