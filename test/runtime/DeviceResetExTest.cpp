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
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/DevMemManager.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "core/DomainSocket.h"
#include "core/FuncSelector.h"
#include "utils/Serialize.h"
#include "mockcpp/mockcpp.hpp"

namespace {
int32_t resetDevice = -1;
std::string resetSummary;
rtError_t RecordDeviceReset(int32_t device) {
    resetDevice = device;
    return RT_ERROR_NONE;
}
int CaptureDeviceSummary(LocalProcess *, const std::string &message) {
    resetSummary = message;
    return message.size();
}
}

TEST(rtDeviceResetEx, runtime_device_is_remapped_only_for_simulator) {
    MOCKER(IsSanitizer).stubs().will(returnValue(false));
    MOCKER(IsOpProf).stubs().will(returnValue(true));
    ProfConfig::Instance().profConfig_.isSimulator = false;
    resetDevice = -1;
    HijackedFuncOfDeviceResetEx instance;
    instance.originfunc_ = RecordDeviceReset;
    EXPECT_EQ(instance.Call(3), RT_ERROR_NONE);
    EXPECT_EQ(resetDevice, 3);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    EXPECT_EQ(instance.Call(3), RT_ERROR_NONE);
    EXPECT_EQ(resetDevice, 0);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    instance.originfunc_ = nullptr;
    EXPECT_EQ(instance.Call(3), RT_ERROR_RESERVED);
    GlobalMockObject::verify();
}

TEST(rtDeviceResetEx, sanitizer_reports_the_reset_device_after_success) {
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(&DevMemManager::Free).stubs();
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(invoke(CaptureDeviceSummary));
    DeviceContext::Instance(3).SetSocVersion("Ascend910B");
    resetSummary.clear();
    HijackedFuncOfDeviceResetEx instance;
    instance.originfunc_ = RecordDeviceReset;
    EXPECT_EQ(instance.Call(3), RT_ERROR_NONE);
    PacketHead head{PacketType::DEVICE_SUMMARY};
    DeviceSummary summary{};
    summary.deviceId = 3;
    summary.device = static_cast<uint32_t>(GetDeviceTypeBySocVersion("Ascend910B"));
    EXPECT_EQ(resetSummary, Serialize(head, summary));
    GlobalMockObject::verify();
}
