/* -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
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
#undef private
#undef protected

#include "mockcpp/mockcpp.hpp"
#include "runtime/RuntimeOrigin.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/ThreadContext.h"
#include "core/FuncSelector.h"
#include "core/HijackedLayerManager.h"

namespace {
constexpr int32_t ACCESSOR_DEVICE = 710;
constexpr int32_t THREAD_DEVICE = 711;
constexpr int32_t PEER_DEVICE = 712;
constexpr int32_t PEER_LOGIC_DEVICE = 812;
constexpr int32_t PEER_PHY_DEVICE = 912;
constexpr uint64_t PEER_ADDR = 0x71000;
constexpr uint64_t PEER_SIZE = 128;
size_t notificationCount = 0;

ssize_t CountNotification(LocalProcess *, std::string) {
    ++notificationCount;
    return 1;
}

rtError_t ConvertPeerPhyToLogic(int32_t phyDeviceId, int32_t *logicDeviceId) {
    EXPECT_EQ(phyDeviceId, PEER_PHY_DEVICE);
    *logicDeviceId = PEER_LOGIC_DEVICE;
    return RT_ERROR_NONE;
}

rtError_t ConvertPeerLogicToVisible(int32_t logicDeviceId, int32_t *visibleDeviceId) {
    EXPECT_EQ(logicDeviceId, PEER_LOGIC_DEVICE);
    *visibleDeviceId = PEER_DEVICE;
    return RT_ERROR_NONE;
}

class RuntimeP2PTest : public testing::Test {
protected:
    void SetUp() override {
        FuncSelector::Instance().Set(ToolType::SANITIZER);
        ThreadContext::Instance().SetDeviceId(THREAD_DEVICE);
        notificationCount = 0;
        PeerAccessState::Instance().Apply(ACCESSOR_DEVICE, -1, PeerAccessOp::RESET);
        PeerAccessState::Instance().Apply(THREAD_DEVICE, -1, PeerAccessOp::RESET);
        MemoryManage::Instance(PEER_DEVICE).CacheMemory<MemoryOpType::FREE>(PEER_ADDR, MemInfoSrc::ACL);
        MOCKER(IsSanitizer).stubs().will(returnValue(true));
        MOCKER(&LocalProcess::Notify).stubs().will(invoke(CountNotification));
    }

    void TearDown() override {
        PeerAccessState::Instance().Apply(ACCESSOR_DEVICE, -1, PeerAccessOp::RESET);
        PeerAccessState::Instance().Apply(THREAD_DEVICE, -1, PeerAccessOp::RESET);
        MemoryManage::Instance(PEER_DEVICE).CacheMemory<MemoryOpType::FREE>(PEER_ADDR, MemInfoSrc::ACL);
        GlobalMockObject::verify();
    }

    void MockPeerConversion() {
        MOCKER(rtsGetLogicDevIdByPhyDevIdOrigin).stubs().will(invoke(ConvertPeerPhyToLogic));
        MOCKER(rtGetVisibleDeviceIdByLogicDeviceIdOrigin).stubs().will(invoke(ConvertPeerLogicToVisible));
    }
};
}

// 测试场景：Runtime Enable 成功，线程当前设备与显式访问方设备不一致。
// 测试预期：按显式设备及转换后的 peer 设备登记授权并发送通知。
TEST_F(RuntimeP2PTest, EnableUsesExplicitDeviceAndConvertsPeerId) {
    MockPeerConversion();
    MemoryManage::Instance(PEER_DEVICE).CacheMemory<MemoryOpType::MALLOC>(PEER_ADDR, MemInfoSrc::ACL, PEER_SIZE);
    HijackedFuncOfEnableP2P enable;
    enable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE, 17);

    EXPECT_EQ(enable.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    EXPECT_EQ(notificationCount, 1);
    const auto accessorPeers = PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE);
    ASSERT_EQ(accessorPeers.size(), 1);
    EXPECT_EQ(accessorPeers[0].deviceId, PEER_DEVICE);
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(THREAD_DEVICE).empty());
}

// 测试场景：Runtime Enable 返回失败。
// 测试预期：不转换 peer 设备号、不登记授权也不发送通知。
TEST_F(RuntimeP2PTest, RuntimeFailureDoesNotConvertOrGrantAccess) {
    MOCKER(rtsGetLogicDevIdByPhyDevIdOrigin).expects(never());
    MOCKER(rtGetVisibleDeviceIdByLogicDeviceIdOrigin).expects(never());
    HijackedFuncOfEnableP2P enable;
    enable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE, 0);

    EXPECT_EQ(enable.Post(RT_ERROR_INVALID_VALUE), RT_ERROR_INVALID_VALUE);
    EXPECT_EQ(notificationCount, 0);
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE).empty());
}

// 测试场景：peer 物理设备号转换逻辑设备号失败。
// 测试预期：保留 Runtime 成功码，不登记授权或发送通知。
TEST_F(RuntimeP2PTest, FailedPeerConversionDoesNotGrantAccess) {
    MOCKER(rtsGetLogicDevIdByPhyDevIdOrigin).stubs().will(returnValue(RT_ERROR_INVALID_VALUE));
    MOCKER(rtGetVisibleDeviceIdByLogicDeviceIdOrigin).expects(never());
    HijackedFuncOfEnableP2P enable;
    enable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE, 0);

    EXPECT_EQ(enable.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    EXPECT_EQ(notificationCount, 0);
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE).empty());
}

// 测试场景：peer 逻辑设备号转换可见设备号失败。
// 测试预期：不登记授权或发送通知。
TEST_F(RuntimeP2PTest, FailedVisibleIdConversionDoesNotGrantAccess) {
    MOCKER(rtsGetLogicDevIdByPhyDevIdOrigin).stubs().will(invoke(ConvertPeerPhyToLogic));
    MOCKER(rtGetVisibleDeviceIdByLogicDeviceIdOrigin).stubs().will(returnValue(RT_ERROR_INVALID_VALUE));
    HijackedFuncOfEnableP2P enable;
    enable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE, 0);

    EXPECT_EQ(enable.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    EXPECT_EQ(notificationCount, 0);
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE).empty());
}

// 测试场景：设备号转换接口成功但返回无效逻辑设备号。
// 测试预期：停止后续转换，不登记授权或发送通知。
TEST_F(RuntimeP2PTest, InvalidConvertedIdDoesNotGrantAccess) {
    MOCKER(rtsGetLogicDevIdByPhyDevIdOrigin).stubs().will(invoke(+[](int32_t, int32_t *logicDeviceId) -> rtError_t {
        *logicDeviceId = -1;
        return RT_ERROR_NONE;
    }));
    MOCKER(rtGetVisibleDeviceIdByLogicDeviceIdOrigin).expects(never());
    HijackedFuncOfEnableP2P enable;
    enable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE, 0);

    EXPECT_EQ(enable.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE).empty());
    EXPECT_EQ(notificationCount, 0);
}

// 测试场景：Runtime Disable 成功且 peer 两步设备号转换成功。
// 测试预期：撤销精确映射和访问授权，并发送撤权通知。
TEST_F(RuntimeP2PTest, DisableRevokesMappingsAndAccessForConvertedPeer) {
    MockPeerConversion();
    MemoryManage::Instance(PEER_DEVICE).CacheMemory<MemoryOpType::MALLOC>(PEER_ADDR, MemInfoSrc::ACL, PEER_SIZE);
    PeerAccessState::Instance().Apply(ACCESSOR_DEVICE, PEER_DEVICE, PeerAccessOp::ENABLE);
    ASSERT_TRUE(MemoryManage::ApplyP2PMapping(ACCESSOR_DEVICE, PEER_ADDR, PEER_SIZE));
    ASSERT_EQ(MemoryManage::SnapshotP2PMappings(ACCESSOR_DEVICE).size(), 1);

    HijackedFuncOfDisableP2P disable;
    disable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE);
    EXPECT_EQ(disable.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    EXPECT_EQ(notificationCount, 2); // 建立映射及成功撤权各通知一次。
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE).empty());
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(ACCESSOR_DEVICE).empty());
}

// 测试场景：Runtime Enable 原始接口符号缺失。
// 测试预期：返回预留错误码，不登记授权或发送通知。
TEST_F(RuntimeP2PTest, MissingOriginReturnsReservedWithoutRecording) {
    MOCKER(rtsGetLogicDevIdByPhyDevIdOrigin).expects(never());
    MOCKER(rtGetVisibleDeviceIdByLogicDeviceIdOrigin).expects(never());
    HijackedFuncOfEnableP2P enable;
    enable.originfunc_ = nullptr;

    EXPECT_EQ(enable.Call(ACCESSOR_DEVICE, PEER_PHY_DEVICE, 0), RT_ERROR_RESERVED);
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE).empty());
}

// 测试场景：Runtime Disable 返回失败。
// 测试预期：已有访问授权和精确映射保持不变。
TEST_F(RuntimeP2PTest, DisableFailurePreservesAccessAndMappings) {
    MemoryManage::Instance(PEER_DEVICE).CacheMemory<MemoryOpType::MALLOC>(PEER_ADDR, MemInfoSrc::ACL, PEER_SIZE);
    PeerAccessState::Instance().Apply(ACCESSOR_DEVICE, PEER_DEVICE, PeerAccessOp::ENABLE);
    ASSERT_TRUE(MemoryManage::ApplyP2PMapping(ACCESSOR_DEVICE, PEER_ADDR, PEER_SIZE));
    MOCKER(rtsGetLogicDevIdByPhyDevIdOrigin).expects(never());
    HijackedFuncOfDisableP2P disable;
    disable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE);

    EXPECT_EQ(disable.Post(RT_ERROR_INVALID_VALUE), RT_ERROR_INVALID_VALUE);
    EXPECT_EQ(PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE).size(), 1);
    EXPECT_EQ(MemoryManage::SnapshotP2PMappings(ACCESSOR_DEVICE).size(), 1);
    EXPECT_EQ(notificationCount, 1);
}

// 测试场景：ACL Enable/Disable 在调用栈中分别调用对应 Runtime 接口。
// 测试预期：层级管理器屏蔽嵌套 Runtime 劫持，避免重复记录 P2P 授权。
TEST_F(RuntimeP2PTest, AclPeerAccessParentsMaskRuntimeP2PHooks) {
    auto &manager = HijackedLayerManager::Instance();
    manager.Layers().clear();

    {
        LayerGuard guard(manager, "aclrtDeviceEnablePeerAccessImpl");
        EXPECT_TRUE(manager.ParentInCallStack("rtEnableP2P"));
        EXPECT_FALSE(manager.ParentInCallStack("rtDisableP2P"));
    }
    {
        LayerGuard guard(manager, "aclrtDeviceDisablePeerAccessImpl");
        EXPECT_TRUE(manager.ParentInCallStack("rtDisableP2P"));
        EXPECT_FALSE(manager.ParentInCallStack("rtEnableP2P"));
    }
    EXPECT_FALSE(manager.ParentInCallStack("rtEnableP2P"));
    EXPECT_FALSE(manager.ParentInCallStack("rtDisableP2P"));
}
