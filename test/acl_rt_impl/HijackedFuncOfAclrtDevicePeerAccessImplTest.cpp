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
#include "acl_rt_impl/HijackedFunc.h"
#undef private
#undef protected

#include "core/FuncSelector.h"
#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/ThreadContext.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"

TEST(AclrtDevicePeerAccessImpl, CapturesDirectionAndPreservesFailure) {
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    ThreadContext::Instance().SetDeviceId(2);
    MOCKER(aclrtGetDeviceImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));
    auto &memory = MemoryManage::Instance(3);
    memory.CacheMemory<MemoryOpType::MALLOC>(0xd000, MemInfoSrc::ACL, 64);
    auto &relations = PeerAccessState::Instance();
    relations.Apply(2, 3, PeerAccessOp::ENABLE);

    HijackedFuncOfAclrtDeviceEnablePeerAccessImpl enable;
    enable.Pre(3, 0);
    EXPECT_EQ(enable.deviceId_, 2);
    EXPECT_EQ(enable.peerDeviceId_, 3);
    EXPECT_EQ(enable.Post(ACL_ERROR_INTERNAL_ERROR), ACL_ERROR_INTERNAL_ERROR);
    ASSERT_EQ(relations.Snapshot(2).size(), 1);

    HijackedFuncOfAclrtDeviceDisablePeerAccessImpl disable;
    disable.Pre(3);
    EXPECT_EQ(disable.deviceId_, 2);
    EXPECT_EQ(disable.peerDeviceId_, 3);
    EXPECT_EQ(disable.Post(ACL_ERROR_INTERNAL_ERROR), ACL_ERROR_INTERNAL_ERROR);
    ASSERT_EQ(relations.Snapshot(2).size(), 1);

    // 成功回调各上报一次；失败回调不能发送授权变更
    LocalDevice::GetInstance(2, CommType::MEMORY);
    MOCKER(&LocalProcess::Notify).expects(exactly(2)).will(returnValue(ssize_t{1}));
    enable.originfunc_ = [](int32_t, uint32_t) { return ACL_SUCCESS; };
    EXPECT_EQ(enable.Call(3, 0), ACL_SUCCESS);
    disable.originfunc_ = [](int32_t) { return ACL_SUCCESS; };
    EXPECT_EQ(disable.Call(3), ACL_SUCCESS);
    relations.Apply(2, 3, PeerAccessOp::DISABLE);
    memory.CacheMemory<MemoryOpType::FREE>(0xd000, MemInfoSrc::ACL);
    GlobalMockObject::verify();
}

TEST(AclrtDevicePeerAccessImpl, MissingOriginReturnsErrorWithoutReporting) {
    MOCKER(&LocalProcess::Notify).expects(never());
    HijackedFuncOfAclrtDeviceEnablePeerAccessImpl enable;
    HijackedFuncOfAclrtDeviceDisablePeerAccessImpl disable;
    enable.originfunc_ = nullptr;
    disable.originfunc_ = nullptr;
    EXPECT_EQ(enable.Call(3, 0), ACL_ERROR_INTERNAL_ERROR);
    EXPECT_EQ(disable.Call(3), ACL_ERROR_INTERNAL_ERROR);
    GlobalMockObject::verify();
}

#include "core/RemoteProcess.h"
#include "runtime/inject_helpers/DevMemManager.h"
#include "utils/Serialize.h"
#include <cstring>

namespace {
std::vector<std::string> peerPackets;
ssize_t CapturePeerPacket(LocalProcess *, std::string packet) {
    peerPackets.push_back(packet);
    return packet.size();
}
}

class PeerOnlineHooks : public testing::Test {
protected:
    void SetUp() override {
        peerPackets.clear();
        ThreadContext::Instance().SetDeviceId(650);
        for (int32_t dev : {650, 651, 652}) {
            PeerAccessState::Instance().Apply(dev, -1, PeerAccessOp::RESET);
        }
        MOCKER(IsSanitizer).stubs().will(returnValue(true));
        MOCKER(aclrtGetDeviceImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));
        MOCKER(&LocalDevice::Notify).stubs().will(invoke(CapturePeerPacket));
        MOCKER(&DevMemManager::Free).stubs();
        // 预建内存通道；真正的 GetInstance/Notify 入口仍使用同一个设备对象。
        for (int32_t dev : {650, 651, 652}) {
            LocalDevice::GetInstance(dev, CommType::MEMORY);
            MemoryManage::Instance(dev).CacheMemory<MemoryOpType::MALLOC>(0x1000, MemInfoSrc::ACL, 64);
        }
    }
    void TearDown() override {
        for (int32_t dev : {650, 651, 652}) {
            PeerAccessState::Instance().Apply(dev, -1, PeerAccessOp::RESET);
            MemoryManage::Instance(dev).CacheMemory<MemoryOpType::FREE>(0x1000, MemInfoSrc::ACL);
        }
        GlobalMockObject::verify();
    }
    std::vector<PeerAccessRecord> Relations() {
        std::vector<PeerAccessRecord> result;
        for (const auto &packet : peerPackets) {
            PacketHead head{};
            std::memcpy(&head, packet.data(), sizeof(head));
            if (head.type == PacketType::PEER_ACCESS_RECORD) {
                PeerAccessRecord record{};
                std::memcpy(&record, packet.data() + sizeof(head), sizeof(record));
                result.push_back(record);
            }
        }
        return result;
    }
};

TEST_F(PeerOnlineHooks, SuccessfulCallbacksReportDirectionAndFailureDoesNotGrantOrRevoke) {
    HijackedFuncOfAclrtDeviceEnablePeerAccessImpl enable;
    enable.Pre(651, 0);
    EXPECT_EQ(enable.Post(ACL_ERROR_INTERNAL_ERROR), ACL_ERROR_INTERNAL_ERROR);
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(650).empty());
    EXPECT_TRUE(Relations().empty());
    EXPECT_EQ(enable.Post(ACL_SUCCESS), ACL_SUCCESS);
    EXPECT_EQ(enable.Post(ACL_SUCCESS), ACL_SUCCESS);
    ASSERT_EQ(PeerAccessState::Instance().Snapshot(650).size(), 1);
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(651).empty());
    HijackedFuncOfAclrtDeviceDisablePeerAccessImpl disable;
    disable.Pre(651);
    EXPECT_EQ(disable.Post(ACL_ERROR_INTERNAL_ERROR), ACL_ERROR_INTERNAL_ERROR);
    EXPECT_EQ(PeerAccessState::Instance().Snapshot(650).size(), 1);
    EXPECT_EQ(disable.Post(ACL_SUCCESS), ACL_SUCCESS);
    EXPECT_TRUE(PeerAccessState::Instance().Snapshot(650).empty());
    const auto records = Relations();
    ASSERT_EQ(records.size(), 3);
    for (const auto &record : records) {
        EXPECT_EQ(record.deviceId, 650);
        EXPECT_EQ(record.peerDeviceId, 651);
    }
    EXPECT_EQ(records[0].operation, PeerAccessOp::ENABLE);
    EXPECT_EQ(records[1].operation, PeerAccessOp::ENABLE);
    EXPECT_EQ(records[2].operation, PeerAccessOp::DISABLE);
}

TEST_F(PeerOnlineHooks, ResetUsesArgumentAndOnlySuccessfulResetClearsAllIncidentEdges) {
    auto &state = PeerAccessState::Instance();
    state.Apply(650, 651, PeerAccessOp::ENABLE);
    state.Apply(651, 650, PeerAccessOp::ENABLE);
    state.Apply(650, 652, PeerAccessOp::ENABLE);
    HijackedFuncOfAclrtResetDeviceImpl reset;
    reset.Pre(651); // 当前线程是 650，RESET 必须使用参数设备 651。
    EXPECT_EQ(reset.Post(ACL_ERROR_INTERNAL_ERROR), ACL_ERROR_INTERNAL_ERROR);
    EXPECT_EQ(state.Snapshot(650).size(), 2);
    EXPECT_EQ(state.Snapshot(651).size(), 1);
    EXPECT_TRUE(Relations().empty());
    EXPECT_EQ(reset.Post(ACL_SUCCESS), ACL_SUCCESS);
    ASSERT_EQ(state.Snapshot(650).size(), 1);
    EXPECT_EQ(state.Snapshot(650)[0].deviceId, 652);
    EXPECT_TRUE(state.Snapshot(651).empty());
    const auto records = Relations();
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records[0].deviceId, 651);
    EXPECT_EQ(records[0].peerDeviceId, -1);
    EXPECT_EQ(records[0].operation, PeerAccessOp::RESET);
}
