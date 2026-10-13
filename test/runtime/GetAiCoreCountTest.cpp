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
#include "runtime/inject_helpers/SimulatorCoreInfo.h"

TEST(SimulatorCoreInfo, finds_existing_and_a5_core_counts_without_310p) {
    uint32_t coreNum = 0;
    EXPECT_TRUE(SimulatorSoc::TryGetSimulatorAiCoreNum("Ascend910B1", coreNum));
    EXPECT_EQ(coreNum, 24U);
    EXPECT_TRUE(SimulatorSoc::TryGetSimulatorAiCoreNum("Ascend950DT_958_future", coreNum));
    EXPECT_EQ(coreNum, 32U);
    EXPECT_FALSE(SimulatorSoc::TryGetSimulatorAiCoreNum("Ascend310P1", coreNum));
    EXPECT_FALSE(SimulatorSoc::TryGetSimulatorAiCoreNum("Ascend950PR_956_future", coreNum));
    EXPECT_EQ(coreNum, 32U);
}

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
    auto expectCoreNum = [&instance, &aiCoreCnt](const char *socVersion, uint32_t expected) {
        ProfConfig::Instance().socVersion_ = socVersion;
        aiCoreCnt = 0;
        EXPECT_EQ(instance.Call(&aiCoreCnt), RT_ERROR_NONE) << socVersion;
        EXPECT_EQ(aiCoreCnt, expected) << socVersion;
    };

    // 已登记型号保持原行为。
    expectCoreNum("Ascend950PR_9599", 36);
    expectCoreNum("Ascend950DT_950x", 8);
    expectCoreNum("Ascend950DT_95A1", 36);

    // 使用未登记的后缀，证明不是全字符串精确匹配。
    expectCoreNum("Ascend950PR_957_future", 28);
    expectCoreNum("Ascend950DT_957_future", 28);
    expectCoreNum("Ascend950PR_958_future", 32);
    expectCoreNum("Ascend950DT_958_future", 32);
    expectCoreNum("Ascend950PR_959_future", 36);
    expectCoreNum("Ascend950DT_959_future", 36);

    // 未定义的 95 系列不能错误落入某个默认核数。
    ProfConfig::Instance().socVersion_ = "Ascend950PR_956_future";
    aiCoreCnt = 123;
    EXPECT_EQ(instance.Call(&aiCoreCnt), RT_ERROR_RESERVED);
    EXPECT_EQ(aiCoreCnt, 123);
    ProfConfig::Instance().socVersion_ = "";
    GlobalMockObject::verify();
}
