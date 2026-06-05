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
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#undef private
#include "core/LocalProcess.h"
#include "mockcpp/mockcpp.hpp"
#include "runtime/HijackedFunc.h"
#include "runtime/RuntimeOrigin.h"
#include "core/FuncSelector.h"
#include "core/DomainSocket.h"
#include "core/LocalProcess.h"

TEST(rtRegisterAllKernel, call_pre_function_with_null_input_expect_return)
{
    rtDevBinary_t *bin = nullptr;
    void **hdl = nullptr;
    HijackedFuncOfRegisterAllKernel instance;
    instance.Pre(bin, hdl);
}

TEST(rtRegisterAllKernel, call_post_function_with_null_input_expect_return)
{
    rtDevBinary_t *bin = nullptr;
    void **hdl = nullptr;
    HijackedFuncOfRegisterAllKernel instance;
    instance.Pre(bin, hdl);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
}

TEST(rtRegisterAllKernel, call_function_with_null_input_expect_return)
{
    rtDevBinary_t *bin = nullptr;
    void **hdl = nullptr;
    HijackedFuncOfRegisterAllKernel instance;
    auto ret = instance.Call(bin, hdl);
    ASSERT_EQ(ret, RT_ERROR_RESERVED);
}

TEST(rtRegisterAllKernel, call_pre_function_with_fuck_input_expect_return)
{
    char fuckData[8] = {0};
    rtDevBinary_t a = {.magic = 0x1, .version = 0x1, .data = fuckData, .length = 8};
    rtDevBinary_t *bin = &a;
    void **hdl = nullptr;
    HijackedFuncOfRegisterAllKernel instance;
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    instance.Pre(bin, hdl);
    GlobalMockObject::verify();
}

TEST(rtRegisterAllKernel, call_post_function_with_fuck_input_expect_return)
{
    char fuckData[8] = {0};
    rtDevBinary_t a = {.magic = 0x1, .version = 0x1, .data = fuckData, .length = 8};
    rtDevBinary_t *bin = &a;
    void **hdl = nullptr;
    HijackedFuncOfRegisterAllKernel instance;
    instance.Pre(bin, hdl);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtRegisterAllKernel, call_function_with_fuck_hdl_input_expect_return)
{
    char fuckData[8] = {0};
    rtDevBinary_t a = {.magic = 0x1, .version = 0x1, .data = fuckData, .length = 8};
    rtDevBinary_t *bin = &a;
    char hdlData[1] = {0};
    void **hdl = reinterpret_cast<void **>(&hdlData);
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(1));
    HijackedFuncOfRegisterAllKernel instance;
    instance.Pre(bin, hdl);
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}

TEST(rtRegisterAllKernel, call_function_with_save_core_file)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    char fuckData[8] = {0};
    rtDevBinary_t a = {.magic = 0x1, .version = 0x1, .data = fuckData, .length = 8};
    rtDevBinary_t *bin = &a;
    char hdlData[1] = {0};
    void **hdl = reinterpret_cast<void **>(&hdlData);
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(-1));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    HijackedFuncOfRegisterAllKernel instance{};
    ProfConfig::Instance().Reset();
    instance.Pre(bin, hdl);
    MOCKER_CPP(&ProfDataCollect::SaveObject, bool(*)(const void *)).stubs().will(returnValue(true));
    ProfConfig::Instance().profConfig_.isSimulator = true;
    auto ret = instance.Post(RT_ERROR_NONE);
    ASSERT_EQ(ret, RT_ERROR_NONE);
    GlobalMockObject::verify();
}
