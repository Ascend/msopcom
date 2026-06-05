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


TEST(rtGetSocVersion, call_function_not_in_prof_logical)
{
    MOCKER(&setsockopt).stubs().will(returnValue(-1));
    MOCKER(&write).stubs().will(returnValue(1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(1));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalProcess::Wait).stubs().will(returnValue(-1));
    HijackedFuncOfGetSocVersion instance;
    char *version = nullptr;
    uint32_t maxLen = 10;
    EXPECT_EQ(instance.Call(version, maxLen), RT_ERROR_RESERVED);

    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    EXPECT_EQ(instance.Call(version, maxLen), RT_ERROR_RESERVED);

    ProfConfig::Instance().socVersion_ = "";
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    EXPECT_EQ(instance.Call(nullptr, maxLen), RT_ERROR_RESERVED);
    EXPECT_EQ(instance.Call(version, maxLen), RT_ERROR_RESERVED);
    GlobalMockObject::verify();
}

TEST(rtGetSocVersion, call_function_in_prof_logical)
{
    MOCKER(&setsockopt).stubs().will(returnValue(-1));
    MOCKER(&write).stubs().will(returnValue(1));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(1));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalProcess::Wait).stubs().will(returnValue(-1));
    ProfConfig::Instance().socVersion_ = "Ascend310P1";
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    HijackedFuncOfGetSocVersion instance;
    char version[100];
    uint32_t maxLen = 100;
    instance.Call(version, maxLen);
    EXPECT_EQ(std::string(version), "Ascend310P1");
    ProfConfig::Instance().socVersion_ = "";
    GlobalMockObject::verify();
}
