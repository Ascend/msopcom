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
#include <cstring>
#include <vector>

#define private public
#define protected public
#include "runtime/HijackedFunc.h"
#undef private
#undef protected

#include "ascendcl/AscendclOrigin.h"
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
// 可见设备列表为 0,2,3 时，物理卡 2 对应进程内设备 1
constexpr int32_t PEER_DEVICE = 1;
constexpr int32_t PEER_PHY_DEVICE = 2;
constexpr int32_t OTHER_PEER_DEVICE = 713;
constexpr uint64_t PEER_ADDR = 0x71000;
constexpr uint64_t OTHER_PEER_ADDR = 0x72000;
constexpr uint64_t PEER_SIZE = 128;
size_t notificationCount = 0;
std::vector<PeerAccessRecord> peerNotifications;

ssize_t CaptureNotification(LocalProcess *, std::string message) {
    ++notificationCount;
    if (message.size() == sizeof(PacketHead) + sizeof(PeerAccessRecord)) {
        PacketHead head{};
        std::memcpy(&head, message.data(), sizeof(head));
        if (head.type == PacketType::PEER_ACCESS_RECORD) {
            PeerAccessRecord record{};
            std::memcpy(&record, message.data() + sizeof(head), sizeof(record));
            peerNotifications.push_back(record);
        }
    }
    return static_cast<ssize_t>(message.size());
}

aclError ConvertPeerPhyToUser(int32_t phyDeviceId, int32_t *userDeviceId) {
    EXPECT_EQ(phyDeviceId, PEER_PHY_DEVICE);
    *userDeviceId = PEER_DEVICE;
    return ACL_SUCCESS;
}

class RuntimeP2PTest : public testing::Test {
protected:
    void SetUp() override {
        FuncSelector::Instance().Set(ToolType::SANITIZER);
        ThreadContext::Instance().SetDeviceId(THREAD_DEVICE);
        notificationCount = 0;
        peerNotifications.clear();
        PeerAccessState::Instance().Apply(ACCESSOR_DEVICE, -1, PeerAccessOp::RESET);
        PeerAccessState::Instance().Apply(THREAD_DEVICE, -1, PeerAccessOp::RESET);
        MemoryManage::Instance(PEER_DEVICE).CacheMemory<MemoryOpType::FREE>(PEER_ADDR, MemInfoSrc::ACL);
        MOCKER(IsSanitizer).stubs().will(returnValue(true));
        MOCKER(&LocalProcess::Notify).stubs().will(invoke(CaptureNotification));
    }

    void TearDown() override {
        PeerAccessState::Instance().Apply(ACCESSOR_DEVICE, -1, PeerAccessOp::RESET);
        PeerAccessState::Instance().Apply(THREAD_DEVICE, -1, PeerAccessOp::RESET);
        MemoryManage::Instance(PEER_DEVICE).CacheMemory<MemoryOpType::FREE>(PEER_ADDR, MemInfoSrc::ACL);
        MemoryManage::Instance(OTHER_PEER_DEVICE).CacheMemory<MemoryOpType::FREE>(OTHER_PEER_ADDR, MemInfoSrc::ACL);
        GlobalMockObject::verify();
    }

    void MockPeerConversion() {
        MOCKER(aclrtGetUserDevIdByPhyDevIdOrigin).expects(once()).will(invoke(ConvertPeerPhyToUser));
        MOCKER(rtGetVisibleDeviceIdByLogicDeviceIdOrigin).expects(never());
    }
};
}

// 测试场景：Runtime Enable 成功，物理卡号与进程内设备号不同，且线程当前设备不是访问方。
// 测试预期：按显式访问方和进程内对端设备登记、上报授权。
TEST_F(RuntimeP2PTest, EnableUsesExplicitDeviceAndConvertsPeerId) {
    MockPeerConversion();
    MemoryManage::Instance(PEER_DEVICE).CacheMemory<MemoryOpType::MALLOC>(PEER_ADDR, MemInfoSrc::ACL, PEER_SIZE);
    HijackedFuncOfEnableP2P enable;
    enable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE, 17);

    EXPECT_EQ(enable.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    EXPECT_EQ(notificationCount, 1);
    ASSERT_EQ(peerNotifications.size(), 1);
    EXPECT_EQ(peerNotifications[0].deviceId, ACCESSOR_DEVICE);
    EXPECT_EQ(peerNotifications[0].peerDeviceId, PEER_DEVICE);
    EXPECT_EQ(peerNotifications[0].operation, PeerAccessOp::ENABLE);
    const auto accessorPeers = PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE);
    ASSERT_EQ(accessorPeers.size(), 1);
    EXPECT_EQ(accessorPeers[0].deviceId, PEER_DEVICE);
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(THREAD_DEVICE).empty());
}

// 测试场景：Runtime Enable 返回失败。
// 测试预期：不转换 peer 设备号、不登记授权也不发送通知。
TEST_F(RuntimeP2PTest, RuntimeFailureDoesNotConvertOrGrantAccess) {
    MOCKER(aclrtGetUserDevIdByPhyDevIdOrigin).expects(never());
    MOCKER(rtGetVisibleDeviceIdByLogicDeviceIdOrigin).expects(never());
    HijackedFuncOfEnableP2P enable;
    enable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE, 0);

    EXPECT_EQ(enable.Post(RT_ERROR_INVALID_VALUE), RT_ERROR_INVALID_VALUE);
    EXPECT_EQ(notificationCount, 0);
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE).empty());
}

// 测试场景：peer 物理设备号转换进程内设备号失败。
// 测试预期：保留 Runtime 成功码，不登记授权或发送通知。
TEST_F(RuntimeP2PTest, FailedPeerConversionDoesNotGrantAccess) {
    MOCKER(aclrtGetUserDevIdByPhyDevIdOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));
    MOCKER(rtGetVisibleDeviceIdByLogicDeviceIdOrigin).expects(never());
    HijackedFuncOfEnableP2P enable;
    enable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE, 0);

    EXPECT_EQ(enable.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    EXPECT_EQ(notificationCount, 0);
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE).empty());
}

// 测试场景：设备号转换接口成功但返回无效的进程内设备号。
// 测试预期：不登记授权或发送通知。
TEST_F(RuntimeP2PTest, InvalidConvertedIdDoesNotGrantAccess) {
    MOCKER(aclrtGetUserDevIdByPhyDevIdOrigin).stubs().will(invoke(+[](int32_t, int32_t *userDeviceId) -> aclError {
        *userDeviceId = -1;
        return ACL_SUCCESS;
    }));
    MOCKER(rtGetVisibleDeviceIdByLogicDeviceIdOrigin).expects(never());
    HijackedFuncOfEnableP2P enable;
    enable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE, 0);

    EXPECT_EQ(enable.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE).empty());
    EXPECT_EQ(notificationCount, 0);
}

// 测试场景：Runtime Disable 成功，访问方同时持有两个对端的授权和精确映射。
// 测试预期：只撤销匹配进程内对端设备的授权和映射，并上报该设备的撤权。
TEST_F(RuntimeP2PTest, DisableRevokesMappingsAndAccessForConvertedPeer) {
    MockPeerConversion();
    MemoryManage::Instance(PEER_DEVICE).CacheMemory<MemoryOpType::MALLOC>(PEER_ADDR, MemInfoSrc::ACL, PEER_SIZE);
    MemoryManage::Instance(OTHER_PEER_DEVICE).CacheMemory<MemoryOpType::MALLOC>(
        OTHER_PEER_ADDR, MemInfoSrc::ACL, PEER_SIZE);
    PeerAccessState::Instance().Apply(ACCESSOR_DEVICE, PEER_DEVICE, PeerAccessOp::ENABLE);
    PeerAccessState::Instance().Apply(ACCESSOR_DEVICE, OTHER_PEER_DEVICE, PeerAccessOp::ENABLE);
    ASSERT_TRUE(MemoryManage::ApplyP2PMapping(ACCESSOR_DEVICE, PEER_ADDR, PEER_SIZE));
    ASSERT_TRUE(MemoryManage::ApplyP2PMapping(ACCESSOR_DEVICE, OTHER_PEER_ADDR, PEER_SIZE));
    ASSERT_EQ(MemoryManage::SnapshotP2PMappings(ACCESSOR_DEVICE).size(), 2);

    HijackedFuncOfDisableP2P disable;
    disable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE);
    EXPECT_EQ(disable.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    EXPECT_EQ(notificationCount, 3); // 两次建立映射和一次撤权
    ASSERT_EQ(peerNotifications.size(), 1);
    EXPECT_EQ(peerNotifications[0].deviceId, ACCESSOR_DEVICE);
    EXPECT_EQ(peerNotifications[0].peerDeviceId, PEER_DEVICE);
    EXPECT_EQ(peerNotifications[0].operation, PeerAccessOp::DISABLE);
    const auto peers = PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE);
    ASSERT_EQ(peers.size(), 1);
    EXPECT_EQ(peers[0].deviceId, OTHER_PEER_DEVICE);
    const auto mappings = MemoryManage::SnapshotP2PMappings(ACCESSOR_DEVICE);
    ASSERT_EQ(mappings.size(), 1);
    EXPECT_EQ(mappings[0].deviceId, OTHER_PEER_DEVICE);
    EXPECT_EQ(mappings[0].addr, OTHER_PEER_ADDR);
}

// 测试场景：Runtime Enable 原始接口符号缺失。
// 测试预期：返回预留错误码，不登记授权或发送通知。
TEST_F(RuntimeP2PTest, MissingOriginReturnsReservedWithoutRecording) {
    MOCKER(aclrtGetUserDevIdByPhyDevIdOrigin).expects(never());
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
    MOCKER(aclrtGetUserDevIdByPhyDevIdOrigin).expects(never());
    HijackedFuncOfDisableP2P disable;
    disable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE);

    EXPECT_EQ(disable.Post(RT_ERROR_INVALID_VALUE), RT_ERROR_INVALID_VALUE);
    EXPECT_EQ(PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE).size(), 1);
    EXPECT_EQ(MemoryManage::SnapshotP2PMappings(ACCESSOR_DEVICE).size(), 1);
    EXPECT_EQ(notificationCount, 1);
}

// 测试场景：Runtime Disable 成功，但对端物理设备号无法转换。
// 测试预期：保留成功返回值，已有授权和映射不变，也不上报撤权。
TEST_F(RuntimeP2PTest, DisableConversionFailureDoesNotRevokeAccess) {
    MemoryManage::Instance(PEER_DEVICE).CacheMemory<MemoryOpType::MALLOC>(PEER_ADDR, MemInfoSrc::ACL, PEER_SIZE);
    PeerAccessState::Instance().Apply(ACCESSOR_DEVICE, PEER_DEVICE, PeerAccessOp::ENABLE);
    ASSERT_TRUE(MemoryManage::ApplyP2PMapping(ACCESSOR_DEVICE, PEER_ADDR, PEER_SIZE));
    MOCKER(aclrtGetUserDevIdByPhyDevIdOrigin).expects(once()).will(returnValue(ACL_ERROR_INTERNAL_ERROR));
    MOCKER(rtGetVisibleDeviceIdByLogicDeviceIdOrigin).expects(never());

    HijackedFuncOfDisableP2P disable;
    disable.Pre(ACCESSOR_DEVICE, PEER_PHY_DEVICE);
    EXPECT_EQ(disable.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    EXPECT_EQ(PeerAccessState::Instance().Snapshot(ACCESSOR_DEVICE).size(), 1);
    EXPECT_EQ(MemoryManage::SnapshotP2PMappings(ACCESSOR_DEVICE).size(), 1);
    EXPECT_TRUE(peerNotifications.empty());
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
