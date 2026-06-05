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


#define private public
#include "runtime/inject_helpers/ProfConfig.h"
#undef private
#include <gtest/gtest.h>
#include <sys/socket.h>
#include "mockcpp/mockcpp.hpp"
#include "runtime/HijackedFunc.h"
#include "core/FuncSelector.h"
#include "core/DomainSocket.h"
#include "core/LocalProcess.h"

TEST(rtGetAiCoreCount, call_function_not_in_prof_logical)
{
    MOCKER(&setsockopt).stubs().will(returnValue(-1));
    MOCKER(&write).stubs().will(returnValue(1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(1));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalProcess::Wait).stubs().will(returnValue(-1));
    FuncSelector::Instance().Set(ToolType::TEST);
    HijackedFuncOfGetAiCoreCount instance;
    uint32_t aiCoreCnt = 1;
    instance.Call(&aiCoreCnt);
    EXPECT_EQ(aiCoreCnt, 1);

    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    instance.Call(&aiCoreCnt);
    EXPECT_EQ(aiCoreCnt, 1);

    ProfConfig::Instance().socVersion_ = "";
    ProfConfig::Instance().profConfig_.isSimulator = true;
    instance.Call(&aiCoreCnt);
    EXPECT_EQ(aiCoreCnt, 1);
    GlobalMockObject::verify();
}

TEST(rtGetAiCoreCount, call_function_in_prof_logical)
{
    MOCKER(&setsockopt).stubs().will(returnValue(-1));
    MOCKER(&write).stubs().will(returnValue(1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(1));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalProcess::Wait).stubs().will(returnValue(-1));
    ProfConfig::Instance().socVersion_ = "Ascend310P1";
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    HijackedFuncOfGetAiCoreCount instance;
    uint32_t aiCoreCnt = 1;
    instance.Call(&aiCoreCnt);
    EXPECT_EQ(aiCoreCnt, 10);
    ProfConfig::Instance().socVersion_ = "Ascend910B1";
    instance.Call(&aiCoreCnt);
    EXPECT_EQ(aiCoreCnt, 24);
    ProfConfig::Instance().socVersion_ = "Ascend950PR_9599";
    instance.Call(&aiCoreCnt);
    EXPECT_EQ(aiCoreCnt, 36);
    ProfConfig::Instance().socVersion_ = "";
    GlobalMockObject::verify();
}
