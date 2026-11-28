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

#include <cstring>
#include <gtest/gtest.h>
#include "mockcpp/mockcpp.hpp"
#include "hccl/HcclWindowCollect.h"
#include "hccl/HcclOrigin.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/ThreadContext.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "helper/MockHelper.h"

namespace {
constexpr int32_t OWNER = 932;
constexpr int32_t WRITER = 933;
constexpr uint64_t BASE = 0xc0000000;
constexpr uint64_t SIZE = 0x20100000; // In + Out + 1MiB Exp，与原告警布局一致
HcclComm COMM = reinterpret_cast<HcclComm>(0x90001);
HcclComm SECOND_COMM = reinterpret_cast<HcclComm>(0x90002);
int phase = 0;
bool queryFails = false;
bool destroyFails = false;
uint32_t queryCount = 0;
std::vector<P2PMappingRecord> events;
ssize_t CaptureWindow(LocalProcess *, std::string packet) {
    PacketHead head{};
    std::memcpy(&head, packet.data(), sizeof(head));
    if (head.type == PacketType::P2P_MAPPING_RECORD) {
        P2PMappingRecord record{};
        std::memcpy(&record, packet.data() + sizeof(head), sizeof(record));
        events.push_back(record);
    }
    return packet.size();
}
HcclResult QuerySize(HcclComm, uint32_t *size) {
    *size = 2;
    return queryFails ? HCCL_E_INTERNAL : HCCL_SUCCESS;
}
HcclResult QueryWindow(HcclComm, uint32_t rank, void **addr, uint64_t *size) {
    ++queryCount;
    if (phase != 0 && rank == 1) {
        *addr = reinterpret_cast<void *>(BASE + (phase == 2 ? 32 : 0));
        *size = phase == 2 ? 64 : SIZE;
    }
    return HCCL_SUCCESS;
}
HcclResult DestroyWindow(HcclComm) { return destroyFails ? HCCL_E_INTERNAL : HCCL_SUCCESS; }
}

// 使用真实分配表与快照，只桩化HCCL查询和报文发送，无NPU依赖
class HcclWindows : public testing::Test {
protected:
    void SetUp() override {
        phase = 1;
        queryFails = false;
        destroyFails = false;
        queryCount = 0;
        events.clear();
        MOCKER(aclrtGetDeviceImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));
        ThreadContext::Instance().SetDeviceId(WRITER);
        LocalDevice::GetInstance(OWNER, CommType::MEMORY);
        LocalDevice::GetInstance(WRITER, CommType::MEMORY);
        MOCKER(&LocalProcess::Notify).stubs().will(invoke(CaptureWindow));
        MOCKER(HcclGetRankSizeOrigin).stubs().will(invoke(QuerySize));
        MOCKER(CommGetRemoteCCLBufOrigin).stubs().will(invoke(QueryWindow));
        MOCKER(HcclCommDestroyOrigin).stubs().will(invoke(DestroyWindow));
        MemoryManage::ResetDevice(OWNER);
        MemoryManage::ResetDevice(WRITER);
        MemoryManage::Instance(OWNER).CacheMemory<MemoryOpType::MALLOC>(BASE, MemInfoSrc::RT, SIZE);
    }
    void TearDown() override {
        destroyFails = false;
        DestroyHcclComm(COMM);
        DestroyHcclComm(SECOND_COMM);
        MemoryManage::ResetDevice(OWNER);
        MemoryManage::ResetDevice(WRITER);
        GlobalMockObject::verify();
    }
    void Collect(HcclComm comm = COMM) {
        RememberHcclComm(WRITER, comm);
        RefreshHcclWindows(WRITER);
    }
};

// 测试场景：按rank查询返回完整CCL，状态区位于base+512MiB
// 测试预期：登记包含Exp的精确窗口，owner由分配表识别，不把rank1当设备号
TEST_F(HcclWindows, CompleteWindowIncludesExpAndUsesAllocationOwner) {
    Collect();
    auto ranges = MemoryManage::SnapshotP2PMappings(WRITER);
    ASSERT_EQ(ranges.size(), 1);
    EXPECT_EQ(ranges[0].addr, BASE);
    EXPECT_EQ(ranges[0].size, SIZE);
    EXPECT_EQ(ranges[0].deviceId, OWNER);
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].scope, reinterpret_cast<uint64_t>(COMM));
    EXPECT_FALSE(events[0].revoke);
    RefreshHcclWindows(WRITER);
    EXPECT_EQ(events.size(), 1); // 窗口未变不重复登记
    RefreshHcclWindows(WRITER + 1);
    EXPECT_EQ(queryCount, 4); // 另一设备不使用该通信域
}

// 测试场景：通信域已发现但尚未创建窗口，稍后启动算子时完成建链
// 测试预期：空窗口不授权，后续查询可登记实际窗口
TEST_F(HcclWindows, LazyResourceCreation) {
    phase = 0;
    Collect();
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(WRITER).empty());
    phase = 1;
    RefreshHcclWindows(WRITER);
    EXPECT_EQ(MemoryManage::SnapshotP2PMappings(WRITER).size(), 1);
}

// 测试场景：查询失败或窗口超出owner分配
// 测试预期：不登记未知范围，已有窗口在查询失效后撤销
TEST_F(HcclWindows, QueryFailureAndOwnerBounds) {
    MemoryManage::Instance(OWNER).CacheMemory<MemoryOpType::FREE>(BASE, MemInfoSrc::RT);
    MemoryManage::Instance(OWNER).CacheMemory<MemoryOpType::MALLOC>(BASE, MemInfoSrc::RT, 64);
    Collect();
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(WRITER).empty());
    phase = 2; // [base+32,base+96)仍超出64B分配
    RefreshHcclWindows(WRITER);
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(WRITER).empty());
    MemoryManage::Instance(OWNER).CacheMemory<MemoryOpType::MALLOC>(BASE, MemInfoSrc::RT, SIZE);
    phase = 1;
    RefreshHcclWindows(WRITER);
    ASSERT_FALSE(MemoryManage::SnapshotP2PMappings(WRITER).empty());
    queryFails = true;
    RefreshHcclWindows(WRITER);
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(WRITER).empty());
    EXPECT_TRUE(events.back().revoke);
}

// 测试场景：多个通信域及普通P2PMap共享同一分配，依次销毁通信域
// 测试预期：失败销毁保持权限；成功销毁仅撤销对应scope，保留其它来源
TEST_F(HcclWindows, DestroyPreservesOtherCommAndP2PMap) {
    Collect();
    Collect(SECOND_COMM);
    ASSERT_TRUE(MemoryManage::ApplyP2PMapping(WRITER, BASE, 32));
    ASSERT_EQ(MemoryManage::SnapshotP2PMappings(WRITER).size(), 3);
    destroyFails = true;
    EXPECT_NE(DestroyHcclComm(COMM), HCCL_SUCCESS);
    EXPECT_EQ(MemoryManage::SnapshotP2PMappings(WRITER).size(), 3);
    destroyFails = false;
    ASSERT_EQ(DestroyHcclComm(COMM), HCCL_SUCCESS);
    EXPECT_EQ(MemoryManage::SnapshotP2PMappings(WRITER).size(), 2);
    ASSERT_EQ(DestroyHcclComm(SECOND_COMM), HCCL_SUCCESS);
    EXPECT_EQ(MemoryManage::SnapshotP2PMappings(WRITER).size(), 1);
}

// 测试场景：owner释放并复用同地址，HCCL仍返回旧窗口
// 测试预期：启动前重复查询不把旧资源重新授权给新分配
TEST_F(HcclWindows, FreeAndAddressReuseCannotResurrectOldWindow) {
    Collect();
    MemoryManage::Instance(OWNER).CacheMemory<MemoryOpType::FREE>(BASE, MemInfoSrc::RT);
    MemoryManage::Instance(OWNER).CacheMemory<MemoryOpType::MALLOC>(BASE, MemInfoSrc::RT, SIZE);
    RefreshHcclWindows(WRITER);
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(WRITER).empty());
}

// 测试场景：窗口改变以及writer reset
// 测试预期：旧区间撤销，reset后不再查询旧句柄，也不保留映射
TEST_F(HcclWindows, WindowReplacementAndDeviceReset) {
    Collect();
    phase = 2;
    RefreshHcclWindows(WRITER);
    auto ranges = MemoryManage::SnapshotP2PMappings(WRITER);
    ASSERT_EQ(ranges.size(), 1);
    EXPECT_EQ(ranges[0].addr, BASE + 32);
    EXPECT_EQ(ranges[0].size, 64);
    const auto before = queryCount;
    MemoryManage::ResetDevice(WRITER);
    RefreshHcclWindows(WRITER);
    EXPECT_EQ(queryCount, before);
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(WRITER).empty());
}

// 测试场景：先发现窗口，owner分配稍后才被采集，窗口地址和大小保持不变
// 测试预期：首次不授权，分配存在后的启动查询可补齐映射
TEST_F(HcclWindows, RetryWindowWhoseOwnerWasNotCollected) {
    MemoryManage::Instance(OWNER).CacheMemory<MemoryOpType::FREE>(BASE, MemInfoSrc::RT);
    Collect();
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(WRITER).empty());
    MemoryManage::Instance(OWNER).CacheMemory<MemoryOpType::MALLOC>(BASE, MemInfoSrc::RT, SIZE);
    RefreshHcclWindows(WRITER);
    EXPECT_EQ(MemoryManage::SnapshotP2PMappings(WRITER).size(), 1);
}
