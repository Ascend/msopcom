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
#include <random>

#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/DomainSocket.h"
#include "core/Communication.h"
#include "runtime/inject_helpers/ConfigManager.h"
#include "runtime/inject_helpers/ThreadContext.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "helper/MockHelper.h"
#include "mockcpp/mockcpp.hpp"
#include "utils/Serialize.h"

namespace {
static int g_curDevId = 10;
aclError GetDeviceOriginStub(int32_t *devId)
{
    *devId = g_curDevId;
    return ACL_ERROR_NONE;
}
void AssureEqSanConfig(const SanitizerConfig &rhs, const SanitizerConfig &lhs)
{
    ASSERT_TRUE((rhs.cacheSize == lhs.cacheSize));
    ASSERT_TRUE(rhs.defaultCheck == lhs.defaultCheck);
    ASSERT_TRUE(rhs.memCheck == lhs.memCheck);
    ASSERT_TRUE(rhs.raceCheck == lhs.raceCheck);
    ASSERT_TRUE(rhs.initCheck == lhs.initCheck);
    ASSERT_TRUE(rhs.syncCheck == lhs.syncCheck);
    ASSERT_TRUE(rhs.registerCheck == lhs.registerCheck);
    ASSERT_TRUE(rhs.traceNonDefaultSprReg == lhs.traceNonDefaultSprReg);
    ASSERT_TRUE(rhs.checkDeviceHeap == lhs.checkDeviceHeap);
    ASSERT_TRUE(rhs.checkCannHeap == lhs.checkCannHeap);
    ASSERT_TRUE(rhs.leakCheck == lhs.leakCheck);
    ASSERT_TRUE(rhs.checkUnusedMemory == lhs.checkUnusedMemory);
    ASSERT_TRUE(rhs.checkBlockId == lhs.checkBlockId);
    ASSERT_TRUE(rhs.cacheSize == lhs.cacheSize);
    ASSERT_STREQ(rhs.pluginPath, rhs.pluginPath);
    ASSERT_STREQ(rhs.kernelName, rhs.kernelName);
}

bool ConnectMocker(void) {
    std::cout << "Calling connect mocker." << std::endl;
    return true;
}

}  // namespace
TEST(ConfigManagerTest, get_equal_config_and_unequal_config_with_different_devID)
{
    Server server(CommType::SOCKET); // 保证Client可以获取SocketPath
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    // 需要确保g_curDevId的LocalDevice未被创建
    int32_t devId = g_curDevId;
    ThreadContext::Instance().SetDeviceId(g_curDevId);
    LocalDevice::GetInstance(g_curDevId);

    MOCKER(&aclrtGetDeviceImplOrigin).stubs().will(invoke(&GetDeviceOriginStub));
    SanitizerConfig config1;
    config1.defaultCheck = false;
    config1.memCheck = true;
    config1.raceCheck = false;
    config1.initCheck = false;
    config1.syncCheck = false;
    config1.registerCheck = false;
    config1.traceNonDefaultSprReg = false;
    config1.checkDeviceHeap = false;
    config1.checkCannHeap = false;
    config1.leakCheck = false;
    config1.checkUnusedMemory = false;
    config1.checkBlockId = CHECK_ALL_BLOCK;
    config1.cacheSize = 1024;
    std::string msg = Serialize(config1);
    MockHelper::Instance().SetMsg(msg);
    MOCKER(&LocalDevice::Wait)
        .stubs()
        .will(invoke(MockHelper::WaitMockFuncF));
    auto gotConfig1 = SanitizerConfigManager::Instance().GetConfig();
    std::cout << "comparing in line:" << __LINE__ << std::endl;
    AssureEqSanConfig(config1, gotConfig1);

    g_curDevId += 1;
    ThreadContext::Instance().SetDeviceId(g_curDevId);
    LocalDevice::GetInstance(g_curDevId);
    config1.defaultCheck = true;
    MockHelper::Instance().SetMsg(Serialize(config1));
    auto gotConfig2 = SanitizerConfigManager::Instance().GetConfig();
    ASSERT_NE(gotConfig1.defaultCheck, gotConfig2.defaultCheck);

    g_curDevId = devId;
    ThreadContext::Instance().SetDeviceId(g_curDevId);
    config1.defaultCheck = false;
    auto gotConfig3 = SanitizerConfigManager::Instance().GetConfig();
    std::cout << "comparing in line:" << __LINE__ << std::endl;
    AssureEqSanConfig(config1, gotConfig3);
    GlobalMockObject::verify();
}

TEST(ConfigManagerTest, get_non_config_content_expect_error_log)
{
    Server server(CommType::SOCKET);
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    // 需要确保g_curDevId的LocalDevice未被创建
    g_curDevId = 12;
    ThreadContext::Instance().SetDeviceId(g_curDevId);
    LocalDevice::GetInstance(g_curDevId);
    MOCKER(&aclrtGetDeviceImplOrigin).stubs().will(invoke(&GetDeviceOriginStub));
    SanitizerConfig config1;
    // 收到多个config
    std::string msg = Serialize(config1, config1);
    MockHelper::Instance().SetMsg(msg);
    MOCKER(&LocalDevice::Wait)
        .stubs()
        .will(invoke(MockHelper::WaitMockFuncF));
    ::testing::internal::CaptureStdout();
    auto gotConfig1 = SanitizerConfigManager::Instance().GetConfig();
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_TRUE(output.find("Init sanitizer config failed") != std::string::npos);
    GlobalMockObject::verify();
}
