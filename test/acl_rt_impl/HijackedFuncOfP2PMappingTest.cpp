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

#include <algorithm>
#include <cstring>
#include <mutex>
#include <thread>
#include <gtest/gtest.h>
#include "mockcpp/mockcpp.hpp"
#define protected public
#include "acl_rt_impl/HijackedFunc.h"
#include "runtime/HijackedFunc.h"
#undef protected
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/InteractHelper.h"
#include "helper/MockHelper.h"
#include "runtime/inject_helpers/ThreadContext.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/DevMemManager.h"
#include "core/FuncSelector.h"
#include "core/HijackedLayerManager.h"

namespace {
std::mutex packetMutex;
std::vector<std::string> packets;
ssize_t CaptureMappingPacket(LocalProcess *, std::string packet) {
    std::lock_guard<std::mutex> lock(packetMutex);
    packets.push_back(packet);
    return packet.size();
}
constexpr int32_t OWNER = 860;
constexpr int32_t ACCESS = 861;
constexpr uint64_t BASE = 0xa10000;
constexpr uint64_t LENGTH = 256;
}

// 无设备依赖：真实运行hook和内存状态，只有工具传输与原Runtime返回值使用桩
class P2PMapping : public testing::Test {
protected:
    void SetUp() override {
        packets.clear();
        ThreadContext::Instance().SetDeviceId(OWNER);
        MOCKER(IsSanitizer).stubs().will(returnValue(true));
        MOCKER(IsOpProf).stubs().will(returnValue(false));
        MOCKER(aclrtGetDeviceImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));
        LocalDevice::GetInstance(OWNER, CommType::MEMORY);
        LocalDevice::GetInstance(ACCESS, CommType::MEMORY);
        MOCKER(&LocalProcess::Notify).stubs().will(invoke(CaptureMappingPacket));
        MemoryManage::ResetDevice(OWNER);
        MemoryManage::ResetDevice(ACCESS);
        MemoryManage::Instance(OWNER).CacheMemory<MemoryOpType::MALLOC>(BASE, MemInfoSrc::ACL, LENGTH);
    }
    void TearDown() override {
        MemoryManage::ResetDevice(OWNER);
        MemoryManage::ResetDevice(ACCESS);
        MemoryManage::ResetDevice(ACCESS + 1);
        GlobalMockObject::verify();
    }
    void Map(uint64_t addr = BASE + 32, uint64_t size = 64) {
        ASSERT_TRUE(MemoryManage::ApplyP2PMapping(ACCESS, addr, size));
    }
};

// 测试场景：当前线程属于所有方，API显式指定另一使用设备
// 测试预期：仅目标设备快照获得精确窗口，线程当前设备保持不变
TEST_F(P2PMapping, ExplicitDestinationAndExactRange) {
    HijackedFuncOfAclrtMemP2PMapImpl hook;
    hook.originfunc_ = [](void *, size_t, int32_t device, uint64_t) {
        EXPECT_EQ(device, ACCESS);
        return ACL_SUCCESS;
    };
    ASSERT_EQ(hook.Call(reinterpret_cast<void *>(BASE + 32), 64, ACCESS, 0), ACL_SUCCESS);
    auto ranges = MemoryManage::SnapshotP2PMappings(ACCESS);
    ASSERT_EQ(ranges.size(), 1);
    EXPECT_EQ(ranges[0].addr, BASE + 32);
    EXPECT_EQ(ranges[0].size, 64);
    EXPECT_EQ(ranges[0].deviceId, OWNER);
    ASSERT_EQ(packets.size(), 1);
    ASSERT_EQ(packets[0].size(), sizeof(PacketHead) + sizeof(P2PMappingRecord));
    PacketHead head{};
    P2PMappingRecord event{};
    std::memcpy(&head, packets[0].data(), sizeof(head));
    std::memcpy(&event, packets[0].data() + sizeof(head), sizeof(event));
    EXPECT_EQ(head.type, PacketType::P2P_MAPPING_RECORD);
    EXPECT_EQ(event.deviceId, ACCESS);
    EXPECT_EQ(event.ownerDeviceId, OWNER);
    EXPECT_EQ(event.ownerAddr, BASE);
    EXPECT_EQ(event.addr, ranges[0].addr);
    EXPECT_EQ(event.size, ranges[0].size);
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(OWNER).empty());
    EXPECT_EQ(ThreadContext::Instance().GetDeviceId(), OWNER);
}

// 测试场景：P2PMap真实接口返回失败
// 测试预期：返回原错误，在线快照不增加任何映射
TEST_F(P2PMapping, FailedApiDoesNotMap) {
    HijackedFuncOfAclrtMemP2PMapImpl hook;
    hook.originfunc_ = [](void *, size_t, int32_t, uint64_t) { return ACL_ERROR_INTERNAL_ERROR; };
    EXPECT_EQ(hook.Call(reinterpret_cast<void *>(BASE), LENGTH, ACCESS, 0), ACL_ERROR_INTERNAL_ERROR);
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(ACCESS).empty());
}

// 测试场景：无所有方、越过原分配、长度溢出或错误设备
// 测试预期：拒绝无法关联的区间，不扩大到整个对端内存
TEST_F(P2PMapping, RejectsUnknownOverflowAndCrossAllocation) {
    EXPECT_FALSE(MemoryManage::ApplyP2PMapping(ACCESS, BASE - 1, 16));
    EXPECT_FALSE(MemoryManage::ApplyP2PMapping(ACCESS, BASE + 128, LENGTH));
    EXPECT_FALSE(MemoryManage::ApplyP2PMapping(ACCESS, UINT64_MAX - 1, 4));
    EXPECT_FALSE(MemoryManage::ApplyP2PMapping(-1, BASE, 16));
    EXPECT_FALSE(MemoryManage::ApplyP2PMapping(ACCESS, BASE, 0));
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(ACCESS).empty());
}

// 测试场景：多个设备分配表出现同一虚拟区间
// 测试预期：拒绝歧义，不按遍历顺序任选一个所有方
TEST_F(P2PMapping, AmbiguousOwnerIsRejected) {
    MemoryManage::Instance(ACCESS + 1).CacheMemory<MemoryOpType::MALLOC>(BASE, MemInfoSrc::ACL, LENGTH);
    EXPECT_FALSE(MemoryManage::ApplyP2PMapping(ACCESS, BASE, 32));
}

// 测试场景：重复映射同一窗口
// 测试预期：快照只有一个区间，映射不增加真实Malloc数量
TEST_F(P2PMapping, RepeatedMappingIsIdempotent) {
    Map();
    Map();
    EXPECT_EQ(MemoryManage::SnapshotP2PMappings(ACCESS).size(), 1);
    EXPECT_EQ(MemoryManage::Instance(OWNER).GetMallocCount(), 1);
    EXPECT_EQ(MemoryManage::Instance(ACCESS).GetMallocCount(), 0);
    EXPECT_EQ(packets.size(), 1);
}

// 测试场景：所有方释放窗口后在同地址重新分配
// 测试预期：旧映射消失，新分配必须重新Map才可访问
TEST_F(P2PMapping, FreeAndAddressReuseDoNotInheritMapping) {
    Map();
    auto &owner = MemoryManage::Instance(OWNER);
    owner.CacheMemory<MemoryOpType::FREE>(BASE, MemInfoSrc::ACL);
    owner.CacheMemory<MemoryOpType::MALLOC>(BASE, MemInfoSrc::ACL, LENGTH);
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(ACCESS).empty());
    Map();
    EXPECT_EQ(MemoryManage::SnapshotP2PMappings(ACCESS).size(), 1);
}

// 测试场景：所有方成功reset
// 测试预期：原分配与所有关联映射失效，不复用旧表推定新映射
TEST_F(P2PMapping, OwnerResetInvalidatesBothTables) {
    Map();
    MemoryManage::ResetDevice(OWNER);
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(ACCESS).empty());
    EXPECT_TRUE(MemoryManage::Instance(OWNER).SnapshotOnlineRanges().empty());
    EXPECT_FALSE(MemoryManage::ApplyP2PMapping(ACCESS, BASE, 32));
}

// 测试场景：使用方reset或显式撤销该方向的访问授权
// 测试预期：仅清理相关映射，原分配保持有效
TEST_F(P2PMapping, AccessResetAndRevokePreserveOwnerAllocation) {
    Map();
    MemoryManage::ResetDevice(ACCESS);
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(ACCESS).empty());
    Map();
    MemoryManage::RevokeP2PMappings(ACCESS, OWNER);
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(ACCESS).empty());
    EXPECT_EQ(MemoryManage::Instance(OWNER).GetMallocCount(), 1);
}

// 测试场景：所有方内存被设为只读后获取映射快照
// 测试预期：窗口保留原始权限，不因P2PMap放开写权限
TEST_F(P2PMapping, SnapshotPreservesOwnerPermission) {
    MemoryManage::Instance(OWNER).SetPermission(BASE, LENGTH, 1);
    Map();
    auto ranges = MemoryManage::SnapshotP2PMappings(ACCESS);
    ASSERT_EQ(ranges.size(), 1);
    EXPECT_EQ(ranges[0].permission, 1);
}

// 测试场景：ACL Map失败、成功，随后按既有Pre流程解除映射
// 测试预期：Map成功登记范围，Unmap在Pre撤销范围并上报释放事件
TEST_F(P2PMapping, AclMapAndUnmapUseExistingReleaseTiming) {
    const auto va = reinterpret_cast<void *>(BASE + 0x1000);
    HijackedFuncOfAclrtMapMemImpl map;
    map.Pre(va, 64, 0, nullptr, 0);
    map.Post(ACL_ERROR_INTERNAL_ERROR);
    EXPECT_TRUE(packets.empty());
    EXPECT_EQ(MemoryManage::Instance().GetMallocCount(), 1);
    map.Post(ACL_SUCCESS);
    EXPECT_EQ(MemoryManage::Instance().GetMallocCount(), 2);
    HijackedFuncOfAclrtUnmapMemImpl unmap;
    unmap.Pre(va);
    ASSERT_EQ(packets.size(), 2);
    HostMemRecord added{};
    HostMemRecord removed{};
    std::memcpy(&added, packets[0].data() + sizeof(PacketHead), sizeof(added));
    std::memcpy(&removed, packets[1].data() + sizeof(PacketHead), sizeof(removed));
    EXPECT_EQ(added.type, MemOpType::MALLOC);
    EXPECT_EQ(added.dstAddr, reinterpret_cast<uint64_t>(va));
    EXPECT_EQ(added.memSize, 64);
    EXPECT_EQ(removed.type, MemOpType::FREE);
    EXPECT_EQ(removed.dstAddr, added.dstAddr);
    EXPECT_EQ(MemoryManage::Instance().GetMallocCount(), 1);
}

// 测试场景：Runtime直调Map成功及既有Unmap撤销流程
// 测试预期：Map失败保持状态、成功登记，Unmap在Pre撤销范围
TEST_F(P2PMapping, RuntimeMapAndUnmapUseExistingReleaseTiming) {
    const auto va = reinterpret_cast<void *>(BASE + 0x1000);
    HijackedFuncOfMapMem map;
    map.Pre(va, 64, 0, nullptr, 0);
    map.Post(RT_ERROR_RESERVED);
    EXPECT_EQ(MemoryManage::Instance().GetMallocCount(), 1);
    map.Post(RT_ERROR_NONE);
    EXPECT_EQ(MemoryManage::Instance().GetMallocCount(), 2);
    HijackedFuncOfUnmapMem unmap;
    unmap.Pre(va);
    EXPECT_EQ(MemoryManage::Instance().GetMallocCount(), 1);
}

// 测试场景：ACL与Runtime嵌套调用Map/Unmap
// 测试预期：复用已有层级管理，底层被上层屏蔽，外层退出后可直调
TEST_F(P2PMapping, ExistingLayerGuardSuppressesNestedRuntimeReports) {
    auto &layers = HijackedLayerManager::Instance();
    {
        LayerGuard guard(layers, "aclrtMapMemImpl");
        EXPECT_TRUE(layers.ParentInCallStack("rtMapMem"));
    }
    {
        LayerGuard guard(layers, "aclrtUnmapMemImpl");
        EXPECT_TRUE(layers.ParentInCallStack("rtUnmapMem"));
    }
    EXPECT_FALSE(layers.ParentInCallStack("rtMapMem"));
}

// 测试场景：所有方增加更窄的EXTRA在线区间后建立映射
// 测试预期：精确窗口也遵守已有来源筛选，不借原Malloc扩大在线范围
TEST_F(P2PMapping, SnapshotRespectsExistingOnlineSourceSelection) {
    MemoryManage::Instance(OWNER).CacheMemory<MemoryOpType::MALLOC>(BASE + 40, MemInfoSrc::EXTRA, 8);
    Map();
    auto ranges = MemoryManage::SnapshotP2PMappings(ACCESS);
    ASSERT_EQ(ranges.size(), 1);
    EXPECT_EQ(ranges[0].addr, BASE + 40);
    EXPECT_EQ(ranges[0].size, 8);
}

// 测试场景：两个host线程同时给不同使用设备登记相同所有方窗口
// 测试预期：目标设备状态独立、各发布一个事件，不串用线程设备号
TEST_F(P2PMapping, ConcurrentThreadsKeepExplicitDestinationsSeparate) {
    LocalDevice::GetInstance(ACCESS + 1, CommType::MEMORY);
    bool first = false;
    bool second = false;
    std::thread a([&] { first = MemoryManage::ApplyP2PMapping(ACCESS, BASE + 32, 32); });
    std::thread b([&] { second = MemoryManage::ApplyP2PMapping(ACCESS + 1, BASE + 64, 16); });
    a.join();
    b.join();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    auto one = MemoryManage::SnapshotP2PMappings(ACCESS);
    auto two = MemoryManage::SnapshotP2PMappings(ACCESS + 1);
    ASSERT_EQ(one.size(), 1);
    ASSERT_EQ(two.size(), 1);
    EXPECT_EQ(one[0].addr, BASE + 32);
    EXPECT_EQ(two[0].addr, BASE + 64);
    EXPECT_EQ(packets.size(), 2);
}

// 测试场景：HCCL辅助线程配对reset成功，但默认Context仍被其它线程引用
// 测试预期：ACL、Runtime和Runtime Ex入口均保留原分配/精确映射且不发送RESET
TEST_F(P2PMapping, ReferenceOnlyResetPreservesAllocationAndMapping) {
    Map();
    const auto generation = DeviceContext::Instance(OWNER).GetContextGeneration();
    packets.clear();
    MOCKER(&DevMemManager::Free).stubs();
    MOCKER(aclrtGetPrimaryCtxStateImplOrigin).stubs().will(invoke(
        +[](int32_t device, uint32_t *flags, int32_t *active) -> aclError {
            EXPECT_EQ(device, OWNER);
            EXPECT_EQ(flags, nullptr);
            *active = 1;
            return ACL_SUCCESS;
        }));
    HijackedFuncOfAclrtResetDeviceImpl aclReset;
    aclReset.originfunc_ = +[](int32_t) -> aclError { return ACL_SUCCESS; };
    EXPECT_EQ(aclReset.Call(OWNER), ACL_SUCCESS);
    HijackedFuncOfDeviceReset runtimeReset;
    runtimeReset.originfunc_ = +[](int32_t) -> rtError_t { return RT_ERROR_NONE; };
    EXPECT_EQ(runtimeReset.Call(OWNER), RT_ERROR_NONE);
    HijackedFuncOfDeviceResetEx runtimeResetEx;
    runtimeResetEx.originfunc_ = runtimeReset.originfunc_;
    EXPECT_EQ(runtimeResetEx.Call(OWNER), RT_ERROR_NONE);
    auto ranges = MemoryManage::Instance(OWNER).SnapshotOnlineRanges();
    ASSERT_EQ(ranges.size(), 1);
    EXPECT_EQ(ranges[0].addr, BASE);
    EXPECT_EQ(ranges[0].size, LENGTH);
    auto mappings = MemoryManage::SnapshotP2PMappings(ACCESS);
    ASSERT_EQ(mappings.size(), 1);
    EXPECT_EQ(mappings[0].addr, BASE + 32);
    EXPECT_EQ(mappings[0].size, 64);
    EXPECT_TRUE(packets.empty());
    EXPECT_EQ(DeviceContext::Instance(OWNER).GetContextGeneration(), generation);
}

// 测试场景：reset成功且默认Context引用已归零
// 测试预期：清理所有方分配和关联映射，并发送设备摘要与一次RESET
TEST_F(P2PMapping, FinalResetRevokesAllocationAndMapping) {
    Map();
    const auto generation = DeviceContext::Instance(OWNER).GetContextGeneration();
    packets.clear();
    MOCKER(&DevMemManager::Free).stubs();
    MOCKER(aclrtGetPrimaryCtxStateImplOrigin).stubs().will(invoke(
        +[](int32_t device, uint32_t *flags, int32_t *active) -> aclError {
            EXPECT_EQ(device, OWNER);
            EXPECT_EQ(flags, nullptr);
            *active = 0;
            return ACL_SUCCESS;
        }));
    DeviceContext::Instance(OWNER).SetSocVersion("Ascend950");
    HijackedFuncOfAclrtResetDeviceImpl reset;
    reset.originfunc_ = +[](int32_t) -> aclError { return ACL_SUCCESS; };
    EXPECT_EQ(reset.Call(OWNER), ACL_SUCCESS);
    EXPECT_TRUE(MemoryManage::Instance(OWNER).SnapshotOnlineRanges().empty());
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(ACCESS).empty());
    ASSERT_EQ(packets.size(), 2);
    PacketHead head{};
    PeerAccessRecord record{};
    ASSERT_EQ(packets[1].size(), sizeof(head) + sizeof(record));
    std::memcpy(&head, packets[1].data(), sizeof(head));
    std::memcpy(&record, packets[1].data() + sizeof(head), sizeof(record));
    EXPECT_EQ(head.type, PacketType::PEER_ACCESS_RECORD);
    EXPECT_EQ(record.deviceId, OWNER);
    EXPECT_EQ(record.operation, PeerAccessOp::RESET);
    EXPECT_EQ(DeviceContext::Instance(OWNER).GetContextGeneration(), generation + 1);
}

// 测试场景：reset成功但默认Context状态接口查询失败
// 测试预期：保守撤销记录，避免把未知寿命的窗口继续授权给远端设备
TEST_F(P2PMapping, UnknownPrimaryContextStateInvalidatesConservatively) {
    Map();
    packets.clear();
    MOCKER(&DevMemManager::Free).stubs();
    MOCKER(aclrtGetPrimaryCtxStateImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));
    DeviceContext::Instance(OWNER).SetSocVersion("Ascend950");
    HijackedFuncOfDeviceReset reset;
    reset.originfunc_ = +[](int32_t) -> rtError_t { return RT_ERROR_NONE; };
    EXPECT_EQ(reset.Call(OWNER), RT_ERROR_NONE);
    EXPECT_TRUE(MemoryManage::Instance(OWNER).SnapshotOnlineRanges().empty());
    EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(ACCESS).empty());
    EXPECT_EQ(packets.size(), 2);
}

// 测试场景：三个reset入口的原API失败，默认Context状态不可据此判断
// 测试预期：不查询状态、不发送RESET，原分配和映射均保留
TEST_F(P2PMapping, FailedResetPreservesAllocationWithoutQuery) {
    Map();
    const auto generation = DeviceContext::Instance(OWNER).GetContextGeneration();
    packets.clear();
    MOCKER(&DevMemManager::Free).stubs();
    MOCKER(aclrtGetPrimaryCtxStateImplOrigin).expects(never());
    HijackedFuncOfAclrtResetDeviceImpl acl;
    acl.Pre(OWNER);
    EXPECT_EQ(acl.Post(ACL_ERROR_INTERNAL_ERROR), ACL_ERROR_INTERNAL_ERROR);
    HijackedFuncOfDeviceReset rt;
    rt.Pre(OWNER);
    EXPECT_EQ(rt.Post(RT_ERROR_RESERVED), RT_ERROR_RESERVED);
    HijackedFuncOfDeviceResetEx ex;
    ex.Pre(OWNER);
    EXPECT_EQ(ex.Post(RT_ERROR_RESERVED), RT_ERROR_RESERVED);
    EXPECT_EQ(MemoryManage::Instance(OWNER).GetMallocCount(), 1);
    EXPECT_EQ(MemoryManage::SnapshotP2PMappings(ACCESS).size(), 1);
    EXPECT_TRUE(packets.empty());
    EXPECT_EQ(DeviceContext::Instance(OWNER).GetContextGeneration(), generation);
}

// 测试场景：Runtime和Runtime Ex分别成功释放默认Context最后一次引用
// 测试预期：两个入口均清理分配与映射，并发布一次目标设备RESET
TEST_F(P2PMapping, RuntimeFinalResetsRevokeAllocationAndMapping) {
    MOCKER(&DevMemManager::Free).stubs();
    MOCKER(aclrtGetPrimaryCtxStateImplOrigin).stubs().will(invoke(
        +[](int32_t, uint32_t *, int32_t *active) -> aclError { *active = 0; return ACL_SUCCESS; }));
    DeviceContext::Instance(OWNER).SetSocVersion("Ascend950");
    for (bool extended : {false, true}) {
        // 每个入口使用独立原分配，避免第二次只验证空表清理
        MemoryManage::ResetDevice(OWNER);
        MemoryManage::Instance(OWNER).CacheMemory<MemoryOpType::MALLOC>(BASE, MemInfoSrc::ACL, LENGTH);
        Map();
        packets.clear();
        if (extended) {
            HijackedFuncOfDeviceResetEx hook;
            hook.Pre(OWNER);
            EXPECT_EQ(hook.Post(RT_ERROR_NONE), RT_ERROR_NONE);
        } else {
            HijackedFuncOfDeviceReset hook;
            hook.Pre(OWNER);
            EXPECT_EQ(hook.Post(RT_ERROR_NONE), RT_ERROR_NONE);
        }
        EXPECT_TRUE(MemoryManage::Instance(OWNER).SnapshotOnlineRanges().empty());
        EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(ACCESS).empty());
        ASSERT_EQ(packets.size(), 2);
        ASSERT_EQ(packets.back().size(), sizeof(PacketHead) + sizeof(PeerAccessRecord));
        PacketHead head{};
        PeerAccessRecord record{};
        std::memcpy(&head, packets.back().data(), sizeof(head));
        std::memcpy(&record, packets.back().data() + sizeof(head), sizeof(record));
        EXPECT_EQ(head.type, PacketType::PEER_ACCESS_RECORD);
        EXPECT_EQ(record.deviceId, OWNER);
        EXPECT_EQ(record.operation, PeerAccessOp::RESET);
    }
}

// 测试场景：ACL和Runtime Ex成功reset后无法查询默认Context状态
// 测试预期：两入口均沿保守清理路径撤销范围，不继续授予未知寿命的映射
TEST_F(P2PMapping, AclAndExtendedResetQueryFailuresInvalidate) {
    MOCKER(&DevMemManager::Free).stubs();
    MOCKER(aclrtGetPrimaryCtxStateImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));
    DeviceContext::Instance(OWNER).SetSocVersion("Ascend950");
    for (bool extended : {false, true}) {
        MemoryManage::ResetDevice(OWNER);
        MemoryManage::Instance(OWNER).CacheMemory<MemoryOpType::MALLOC>(BASE, MemInfoSrc::ACL, LENGTH);
        Map();
        packets.clear();
        if (extended) {
            HijackedFuncOfDeviceResetEx hook;
            hook.Pre(OWNER);
            EXPECT_EQ(hook.Post(RT_ERROR_NONE), RT_ERROR_NONE);
        } else {
            HijackedFuncOfAclrtResetDeviceImpl hook;
            hook.Pre(OWNER);
            EXPECT_EQ(hook.Post(ACL_SUCCESS), ACL_SUCCESS);
        }
        EXPECT_TRUE(MemoryManage::Instance(OWNER).SnapshotOnlineRanges().empty());
        EXPECT_TRUE(MemoryManage::SnapshotP2PMappings(ACCESS).empty());
        EXPECT_EQ(packets.size(), 2);
    }
}

// 测试场景：映射使用方配对reset成功，但仍有默认Context引用
// 测试预期：保留两侧分配、使用方映射及设备状态，不发送清理消息
TEST_F(P2PMapping, AccessorReferenceResetPreservesBothDevices) {
    MemoryManage::Instance(ACCESS).CacheMemory<MemoryOpType::MALLOC>(BASE + LENGTH, MemInfoSrc::ACL, LENGTH);
    Map();
    packets.clear();
    MOCKER(&DevMemManager::Free).stubs();
    MOCKER(aclrtGetPrimaryCtxStateImplOrigin).stubs().will(invoke(
        +[](int32_t device, uint32_t *, int32_t *active) -> aclError {
            EXPECT_EQ(device, ACCESS);
            *active = 1;
            return ACL_SUCCESS;
        }));
    HijackedFuncOfAclrtResetDeviceImpl hook;
    hook.Pre(ACCESS);
    EXPECT_EQ(hook.Post(ACL_SUCCESS), ACL_SUCCESS);
    EXPECT_EQ(MemoryManage::Instance(OWNER).GetMallocCount(), 1);
    EXPECT_EQ(MemoryManage::Instance(ACCESS).GetMallocCount(), 1);
    auto mappings = MemoryManage::SnapshotP2PMappings(ACCESS);
    ASSERT_EQ(mappings.size(), 1);
    EXPECT_EQ(mappings[0].addr, BASE + 32);
    EXPECT_EQ(mappings[0].size, 64);
    EXPECT_TRUE(packets.empty());
}

// 测试场景：真实IPC响应提供逻辑窗口，再失败关闭、成功关闭
// 测试预期：在线快照仅增加精确窗口，失败保持、成功撤销，且不重复发MALLOC
TEST_F(P2PMapping, IPCResponseMaintainsExactOnlineWindow)
{
    ThreadContext::Instance().SetDeviceId(ACCESS);
    MOCKER(&LocalDevice::Wait).stubs().will(invoke(MockHelper::WaitMockFuncF));
    auto respond = [](IPCOperationType type, ResponseStatus status, uint64_t size) {
        MockHelper::Instance().SetMsg(Serialize(PacketType::IPC_RESPONSE, IPCResponse{type, status, size}));
    };
    IPCMemRecord map{};
    map.type = IPCOperationType::MAP_INFO;
    map.mapInfo.addr = BASE + 0x10000;
    respond(map.type, ResponseStatus::FAIL, 64);
    IPCInteract(map);
    EXPECT_TRUE(MemoryManage::Instance().SnapshotOnlineRanges().empty());
    respond(map.type, ResponseStatus::SUCCESS, 64);
    IPCInteract(map);
    auto ranges = MemoryManage::Instance().SnapshotOnlineRanges();
    ASSERT_EQ(ranges.size(), 1U);
    EXPECT_EQ(ranges[0].addr, map.mapInfo.addr);
    EXPECT_EQ(ranges[0].size, 64U);
    EXPECT_EQ(MemoryManage::Instance().GetMallocCount(), 1U);
    IPCMemRecord close{};
    close.type = IPCOperationType::UNMAP_INFO;
    close.unmapInfo.addr = map.mapInfo.addr;
    respond(close.type, ResponseStatus::FAIL, 0);
    IPCInteract(close);
    ASSERT_EQ(MemoryManage::Instance().SnapshotOnlineRanges().size(), 1U);
    respond(close.type, ResponseStatus::SUCCESS, 0);
    IPCInteract(close);
    EXPECT_TRUE(MemoryManage::Instance().SnapshotOnlineRanges().empty());
    EXPECT_EQ(MemoryManage::Instance().GetMallocCount(), 0U);
    EXPECT_EQ(packets.size(), 4U);
}

// 测试场景：IPC成功响应的窗口为零、溢出，或响应操作与请求不匹配
// 测试预期：不建立在线合法范围，不扩大授权边界
TEST_F(P2PMapping, InvalidIPCResponseCannotAuthorizeOnlineMemory)
{
    ThreadContext::Instance().SetDeviceId(ACCESS);
    MOCKER(&LocalDevice::Wait).stubs().will(invoke(MockHelper::WaitMockFuncF));
    IPCMemRecord map{};
    map.type = IPCOperationType::MAP_INFO;
    map.mapInfo.addr = UINT64_MAX - 15;
    for (auto response : {IPCResponse{map.type, ResponseStatus::SUCCESS, 0},
                          IPCResponse{map.type, ResponseStatus::SUCCESS, 32},
                          IPCResponse{IPCOperationType::UNMAP_INFO, ResponseStatus::SUCCESS, 8}}) {
        MockHelper::Instance().SetMsg(Serialize(PacketType::IPC_RESPONSE, response));
        IPCInteract(map);
        EXPECT_TRUE(MemoryManage::Instance().SnapshotOnlineRanges().empty());
    }
}
