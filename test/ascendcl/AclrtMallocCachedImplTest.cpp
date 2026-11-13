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
#include "acl_rt_impl/HijackedFunc.h"
#undef private
#include "core/DomainSocket.h"
#include "core/FuncSelector.h"
#include "core/LocalProcess.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/ThreadContext.h"
#include "runtime/inject_helpers/MemGuard.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "utils/Serialize.h"
#include "mockcpp/mockcpp.hpp"

TEST(aclrtMallocCached, sanitizer_call_pre_function_with_malloc_args_expect_save_correct_params)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));

    void *devPtr = nullptr;
    HijackedFuncOfAclrtMallocCachedImpl instance;
    instance.Pre(&devPtr, 64, ACL_MEM_MALLOC_HUGE_FIRST);
    ASSERT_EQ(instance.devPtr_, &devPtr);
    ASSERT_EQ(instance.size_, 64ULL);
    GlobalMockObject::verify();
}

TEST(aclrtMallocCached, sanitizer_call_post_function_with_malloc_args_expect_return)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));

    void *devPtr = nullptr;
    HijackedFuncOfAclrtMallocCachedImpl instance;
    instance.Pre(&devPtr, 64, ACL_MEM_MALLOC_HUGE_FIRST);
    ASSERT_EQ(instance.Post(ACL_ERROR_NONE), ACL_ERROR_NONE);
    GlobalMockObject::verify();
}


TEST(aclrtMallocCached, TracksSuccessfulAllocationAndIgnoresFailure)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(IsOpProf).stubs().will(returnValue(false));
    MOCKER(&ThreadContext::GetDeviceId).stubs().will(returnValue(766));
    MOCKER(&MemoryGuard::MallocProc).stubs();
    MOCKER(&DeviceContext::IsAscend950).stubs().will(returnValue(false));
    MOCKER(&LocalProcess::Notify).expects(once()).will(returnValue(0));
    void *pointer = reinterpret_cast<void *>(0xccae000);
    auto &memory = MemoryManage::Instance(766);
    const auto count = memory.GetMallocCount();
    HijackedFuncOfAclrtMallocCachedImpl instance;
    instance.Pre(&pointer, 64, ACL_MEM_MALLOC_HUGE_FIRST);
    EXPECT_EQ(instance.Post(1), 1);
    EXPECT_EQ(memory.GetMallocCount(), count);
    EXPECT_EQ(instance.Post(ACL_ERROR_NONE), ACL_ERROR_NONE);
    const auto ranges = memory.SnapshotOnlineRanges();
    EXPECT_EQ(ranges.size(), 1);
    if (!ranges.empty()) {
        EXPECT_EQ(ranges.front().addr, reinterpret_cast<uint64_t>(pointer));
        EXPECT_EQ(ranges.front().size, 96);
    }
    memory.CacheMemory<MemoryOpType::FREE>(reinterpret_cast<uint64_t>(pointer), MemInfoSrc::ACL);
    EXPECT_EQ(memory.GetMallocCount(), count);
    GlobalMockObject::verify();
}

namespace {
std::vector<HostMemRecord> cachedRecords;
ssize_t CaptureCachedRecord(LocalProcess *, std::string packet)
{
    PacketHead head{};
    HostMemRecord record{};
    EXPECT_TRUE(Deserialize(packet, head));
    EXPECT_EQ(head.type, PacketType::MEMORY_RECORD);
    EXPECT_TRUE(Deserialize(packet.substr(sizeof(head)), record));
    cachedRecords.push_back(record);
    return packet.size();
}

class CachedMallocHook : public HijackedFuncOfAclrtMallocCachedImpl {
public:
    CachedMallocHook()
    {
        originfunc_ = +[](void **pointer, size_t size, aclrtMemMallocPolicy) -> aclError {
            EXPECT_EQ(size, 64);
            *pointer = reinterpret_cast<void *>(0xccaf000);
            return ACL_ERROR_NONE;
        };
    }
};

class CachedFreeHook : public HijackedFuncOfAclrtFreeImpl {
public:
    CachedFreeHook()
    {
        originfunc_ = +[](void *pointer) -> aclError {
            EXPECT_EQ(pointer, reinterpret_cast<void *>(0xccaf000));
            return ACL_ERROR_NONE;
        };
    }
};
} // namespace

class CachedMallocLifecycle : public testing::TestWithParam<std::tuple<bool, aclrtMemMallocPolicy>> {
protected:
    void SetUp() override
    {
        cachedRecords.clear();
        MOCKER(IsSanitizer).stubs().will(returnValue(true));
        MOCKER(IsOpProf).stubs().will(returnValue(false));
        MOCKER(&DeviceContext::IsAscend950).stubs().will(returnValue(std::get<0>(GetParam())));
        MOCKER(&ThreadContext::GetDeviceId).stubs().will(returnValue(767));
        MOCKER(&MemoryGuard::GetTotalSize).stubs().will(returnValue(size_t{64}));
        MOCKER(&MemoryGuard::MallocProc).stubs();
        MOCKER(&MemoryGuard::GetRealPtr).stubs().will(returnValue(reinterpret_cast<void *>(0xccaf000)));
        MOCKER(&MemoryGuard::FreeProc).stubs();
        MOCKER(&LocalDevice::Notify).stubs().will(invoke(CaptureCachedRecord));
    }
    void TearDown() override
    {
        // ASSERT 提前退出时也撤销本 fixture 的固定分配，避免污染后续参数组合。
        MemoryManage::Instance(767).CacheMemory<MemoryOpType::FREE>(0xccaf000, MemInfoSrc::ACL);
        GlobalMockObject::verify();
    }
};

TEST_P(CachedMallocLifecycle, ReportsAlignedRangeAndRealFreeHookRemovesIt)
{
    auto &memory = MemoryManage::Instance(767);
    const auto before = memory.GetMallocCount();
    const auto policy = std::get<1>(GetParam());
    const bool noPadding = std::get<0>(GetParam()) || policy == ACL_MEM_MALLOC_HUGE1G_ONLY ||
        policy == ACL_MEM_MALLOC_HUGE1G_ONLY_P2P;
    void *pointer = nullptr;
    CachedMallocHook allocation;
    ASSERT_EQ(allocation.Call(&pointer, 33, policy), ACL_ERROR_NONE);
    EXPECT_EQ(memory.GetMallocCount(), before + 1);
    const auto ranges = memory.SnapshotOnlineRanges();
    ASSERT_EQ(ranges.size(), 1);
    EXPECT_EQ(ranges.front().size, noPadding ? 64 : 96);
    CachedFreeHook release;
    EXPECT_EQ(release.Call(pointer), ACL_ERROR_NONE);
    EXPECT_EQ(memory.GetMallocCount(), before);
    EXPECT_TRUE(memory.SnapshotOnlineRanges().empty());
    ASSERT_EQ(cachedRecords.size(), 2);
    EXPECT_EQ(cachedRecords[0].type, MemOpType::MALLOC);
    EXPECT_EQ(cachedRecords[0].memSize, noPadding ? 64 : 96);
    EXPECT_EQ(cachedRecords[1].type, MemOpType::FREE);
    for (const auto &record : cachedRecords) {
        EXPECT_EQ(record.dstAddr, reinterpret_cast<uint64_t>(pointer));
        EXPECT_EQ(record.infoSrc, MemInfoSrc::ACL);
    }
}

INSTANTIATE_TEST_SUITE_P(ChipAndPolicy, CachedMallocLifecycle,
    testing::Combine(testing::Bool(), testing::Values(ACL_MEM_MALLOC_HUGE_FIRST,
        ACL_MEM_MALLOC_HUGE1G_ONLY, ACL_MEM_MALLOC_HUGE1G_ONLY_P2P)));

TEST(aclrtMallocCached, NonSanitizerDoesNotCacheOrReportAllocation)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(false));
    MOCKER(IsOpProf).stubs().will(returnValue(false));
    MOCKER(&LocalDevice::Notify).expects(never());
    MOCKER(&ThreadContext::GetDeviceId).stubs().will(returnValue(767));
    auto &memory = MemoryManage::Instance(767);
    const auto before = memory.GetMallocCount();
    void *pointer = reinterpret_cast<void *>(0xccaf000);
    HijackedFuncOfAclrtMallocCachedImpl allocation;
    allocation.Pre(&pointer, 33, ACL_MEM_MALLOC_HUGE_FIRST);
    EXPECT_EQ(allocation.Post(ACL_ERROR_NONE), ACL_ERROR_NONE);
    EXPECT_EQ(memory.GetMallocCount(), before);
    GlobalMockObject::verify();
}
