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
#include <vector>
#define private public
#include "runtime/HijackedFunc.h"
#include "runtime/inject_helpers/MemoryContext.h"
#undef private
#include "core/FuncSelector.h"
#include "core/DomainSocket.h"
#include "runtime/RuntimeOrigin.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "acl_rt_impl/HijackedFunc.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/ThreadContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "utils/Serialize.h"
#include "mockcpp/mockcpp.hpp"

TEST(rtMapMem, sanitizer_call_pre_function_with_map_mem_args_expect_save_correct_params)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));

    int stubObj;
    void *devPtr = &stubObj;
    HijackedFuncOfMapMem instance;
    instance.Pre(devPtr, 64, 0, nullptr, 0);
    ASSERT_EQ(instance.devPtr_, devPtr);
    ASSERT_EQ(instance.size_, 64UL);
    GlobalMockObject::verify();
}

TEST(rtMapMem, sanitizer_call_post_function_with_map_mem_args_expect_return)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(rtGetDeviceOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));

    int stubObj;
    void *devPtr = &stubObj;
    HijackedFuncOfMapMem instance;
    instance.Pre(devPtr, 64, 0, nullptr, 0);
    ASSERT_EQ(instance.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    GlobalMockObject::verify();
}

// RT/ACL 两层都必须在操作成功后同步本地表，失败时保留原有映射。

struct RtMapping {
    using Map = HijackedFuncOfMapMem;
    using Unmap = HijackedFuncOfUnmapMem;
    static MemInfoSrc Source() { return MemInfoSrc::RT; }
    static rtError_t Success() { return RT_ERROR_NONE; }
    static rtError_t Failure() { return RT_ERROR_INVALID_VALUE; }
};
struct AclMapping {
    using Map = HijackedFuncOfAclrtMapMemImpl;
    using Unmap = HijackedFuncOfAclrtUnmapMemImpl;
    static MemInfoSrc Source() { return MemInfoSrc::ACL; }
    static aclError Success() { return ACL_ERROR_NONE; }
    static aclError Failure() { return 1; }
};

namespace {
std::vector<std::string> mappingPackets;
ssize_t CaptureMappingPacket(LocalProcess *, std::string packet)
{
    mappingPackets.push_back(packet);
    return packet.size();
}

template <typename Hook>
class MappingHook : public Hook {
public:
    explicit MappingHook(typename Hook::FuncType origin) { this->originfunc_ = origin; }
};
} // namespace

template <typename Api>
class MappedMemory : public testing::Test {
protected:
    void *address = reinterpret_cast<void *>(0xccad000);
    MemoryManage &memory = MemoryManage::Instance(765);
    void ExpectRecord(size_t index, MemOpType type, uint64_t size)
    {
        ASSERT_LT(index, mappingPackets.size());
        const auto &packet = mappingPackets[index];
        ASSERT_EQ(packet.size(), sizeof(PacketHead) + sizeof(HostMemRecord));
        PacketHead head{};
        HostMemRecord record{};
        ASSERT_TRUE(Deserialize(packet, head));
        ASSERT_TRUE(Deserialize(packet.substr(sizeof(head)), record));
        EXPECT_EQ(head.type, PacketType::MEMORY_RECORD);
        EXPECT_EQ(record.type, type);
        EXPECT_EQ(record.infoSrc, Api::Source());
        EXPECT_EQ(record.dstAddr, reinterpret_cast<uint64_t>(address));
        EXPECT_EQ(record.memSize, size);
        EXPECT_EQ(record.infoDesc, MemInfoDesc::DEFAULT);
        EXPECT_EQ(record.srcAddr, 0);
        EXPECT_EQ(record.paramsNo, 0);
        EXPECT_EQ(record.rootAddr, 0);
    }
    bool ContainsMapping(uint64_t size = 64)
    {
        for (const auto &range : memory.SnapshotOnlineRanges()) {
            if (range.addr == reinterpret_cast<uint64_t>(address) && range.size == size) {
                return true;
            }
        }
        return false;
    }
    size_t MappingCount()
    {
        size_t count = 0;
        for (const auto &range : memory.SnapshotOnlineRanges()) {
            if (range.addr == reinterpret_cast<uint64_t>(address)) { ++count; }
        }
        return count;
    }
    void SetUp() override
    {
        mappingPackets.clear();
        MOCKER(&LocalDevice::Notify).stubs().will(invoke(CaptureMappingPacket));
        MOCKER(IsSanitizer).stubs().will(returnValue(true));
        MOCKER(IsOpProf).stubs().will(returnValue(false));
        MOCKER(&ThreadContext::GetDeviceId).stubs().will(returnValue(765));
    }
    void TearDown() override
    {
        // 即使断言提前退出或映射大小发生变化，也清理本用例持有的地址。
        while (MappingCount() != 0) {
            memory.CacheMemory<MemoryOpType::FREE>(reinterpret_cast<uint64_t>(address), Api::Source());
        }
        GlobalMockObject::verify();
    }
};
using MappingApis = testing::Types<RtMapping, AclMapping>;
TYPED_TEST_SUITE(MappedMemory, MappingApis);

TYPED_TEST(MappedMemory, SuccessfulMapAndUnmapUpdateLocalTable)
{
    typename TypeParam::Map map;
    map.Pre(this->address, 64, 0, nullptr, 0);
    EXPECT_EQ(map.Post(TypeParam::Success()), TypeParam::Success());
    EXPECT_TRUE(this->ContainsMapping());
    typename TypeParam::Unmap unmap;
    unmap.Pre(this->address);
    EXPECT_TRUE(this->ContainsMapping());
    EXPECT_EQ(unmap.Post(TypeParam::Success()), TypeParam::Success());
    EXPECT_FALSE(this->ContainsMapping());
    ASSERT_EQ(mappingPackets.size(), 2);
    this->ExpectRecord(0, MemOpType::MALLOC, 64);
    this->ExpectRecord(1, MemOpType::FREE, 0);
}

TYPED_TEST(MappedMemory, FailedMapDoesNotAuthorizeOrReportMemory)
{
    typename TypeParam::Map map;
    map.Pre(this->address, 64, 0, nullptr, 0);
    EXPECT_EQ(map.Post(TypeParam::Failure()), TypeParam::Failure());
    EXPECT_FALSE(this->ContainsMapping());
    EXPECT_TRUE(mappingPackets.empty());
}

TYPED_TEST(MappedMemory, FailedUnmapPreservesMemoryUntilSuccessfulRetry)
{
    typename TypeParam::Map map;
    map.Pre(this->address, 64, 0, nullptr, 0);
    ASSERT_EQ(map.Post(TypeParam::Success()), TypeParam::Success());
    typename TypeParam::Unmap unmap;
    unmap.Pre(this->address);
    EXPECT_TRUE(this->ContainsMapping());
    ASSERT_EQ(mappingPackets.size(), 1);
    EXPECT_EQ(unmap.Post(TypeParam::Failure()), TypeParam::Failure());
    EXPECT_TRUE(this->ContainsMapping());
    ASSERT_EQ(mappingPackets.size(), 1);
    EXPECT_EQ(unmap.Post(TypeParam::Success()), TypeParam::Success());
    EXPECT_FALSE(this->ContainsMapping());
    ASSERT_EQ(mappingPackets.size(), 2);
    this->ExpectRecord(0, MemOpType::MALLOC, 64);
    this->ExpectRecord(1, MemOpType::FREE, 0);
}

TYPED_TEST(MappedMemory, FailedUnmapCallPreservesRangeAndDoesNotSendFree)
{
    typename TypeParam::Map map;
    map.Pre(this->address, 64, 0, nullptr, 0);
    ASSERT_EQ(map.Post(TypeParam::Success()), TypeParam::Success());
    MappingHook<typename TypeParam::Unmap> unmap(+[](void *address) -> decltype(TypeParam::Success()) {
        EXPECT_EQ(address, reinterpret_cast<void *>(0xccad000));
        EXPECT_EQ(mappingPackets.size(), 1);
        EXPECT_FALSE(MemoryManage::Instance(765).SnapshotOnlineRanges().empty());
        return TypeParam::Failure();
    });
    EXPECT_EQ(unmap.Call(this->address), TypeParam::Failure());
    EXPECT_TRUE(this->ContainsMapping());
    ASSERT_EQ(mappingPackets.size(), 1);
    this->ExpectRecord(0, MemOpType::MALLOC, 64);
}

TYPED_TEST(MappedMemory, MissingUnmapSymbolDoesNotRemoveRangeOrSendFree)
{
    typename TypeParam::Map map;
    map.Pre(this->address, 64, 0, nullptr, 0);
    ASSERT_EQ(map.Post(TypeParam::Success()), TypeParam::Success());
    MappingHook<typename TypeParam::Unmap> unmap(nullptr);
    EXPECT_NE(unmap.Call(this->address), TypeParam::Success());
    EXPECT_TRUE(this->ContainsMapping());
    EXPECT_EQ(mappingPackets.size(), 1);
}

TYPED_TEST(MappedMemory, FailedSecondUnmapDoesNotSendDuplicateFree)
{
    typename TypeParam::Map map;
    typename TypeParam::Unmap unmap;
    map.Pre(this->address, 64, 0, nullptr, 0);
    map.Post(TypeParam::Success());
    unmap.Pre(this->address);
    unmap.Post(TypeParam::Success());
    unmap.Pre(this->address);
    EXPECT_EQ(unmap.Post(TypeParam::Failure()), TypeParam::Failure());
    EXPECT_FALSE(this->ContainsMapping());
    ASSERT_EQ(mappingPackets.size(), 2);
    this->ExpectRecord(1, MemOpType::FREE, 0);
}

TYPED_TEST(MappedMemory, FailedRemapPreservesExistingMappingAndSuccessfulRemapCanBeFreed)
{
    typename TypeParam::Map map;
    typename TypeParam::Unmap unmap;
    map.Pre(this->address, 64, 0, nullptr, 0);
    map.Post(TypeParam::Success());
    map.Pre(this->address, 128, 0, nullptr, 0);
    EXPECT_EQ(map.Post(TypeParam::Failure()), TypeParam::Failure());
    EXPECT_TRUE(this->ContainsMapping());
    EXPECT_FALSE(this->ContainsMapping(128));
    EXPECT_EQ(this->MappingCount(), 1);
    ASSERT_EQ(this->memory.SnapshotOnlineRanges().size(), 1);
    EXPECT_EQ(this->memory.SnapshotOnlineRanges().front().size, 64);
    ASSERT_EQ(mappingPackets.size(), 1);
    unmap.Pre(this->address);
    unmap.Post(TypeParam::Success());
    map.Pre(this->address, 64, 0, nullptr, 0);
    map.Post(TypeParam::Success());
    EXPECT_TRUE(this->ContainsMapping());
    unmap.Pre(this->address);
    unmap.Post(TypeParam::Success());
    EXPECT_FALSE(this->ContainsMapping());
    ASSERT_EQ(mappingPackets.size(), 4);
    this->ExpectRecord(2, MemOpType::MALLOC, 64);
    this->ExpectRecord(3, MemOpType::FREE, 0);
}

template <typename Api>
class ProfiledMapping : public testing::Test {
protected:
    void *address = reinterpret_cast<void *>(0xccad000);
    MemoryContext &memory = MemoryContext::Instance();
    void SetUp() override
    {
        MOCKER(IsSanitizer).stubs().will(returnValue(false));
        MOCKER(IsOpProf).stubs().will(returnValue(true));
        MOCKER(&ProfConfig::IsSimulator).stubs().will(returnValue(false));
        MOCKER(&ProfConfig::IsAppReplay).stubs().will(returnValue(false));
        MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(false));
        MOCKER(&DeviceContext::GetRunningDeviceId).stubs().will(returnValue(766));
        memory.Discard(address);
    }
    void TearDown() override
    {
        memory.Discard(address);
        GlobalMockObject::verify();
    }
    bool ContainsMapping(uint64_t size = 64) { return memory.GetBucket().memSectionMap.count(address) != 0; }
};
TYPED_TEST_SUITE(ProfiledMapping, MappingApis);

TYPED_TEST(ProfiledMapping, FailedUnmapPreservesReplayMemoryUntilSuccessfulRetry)
{
    typename TypeParam::Map map;
    map.Pre(this->address, 64, 0, nullptr, 0);
    ASSERT_EQ(map.Post(TypeParam::Success()), TypeParam::Success());
    ASSERT_TRUE(this->ContainsMapping());
    typename TypeParam::Unmap unmap;
    unmap.Pre(this->address);
    EXPECT_TRUE(this->ContainsMapping());
    EXPECT_EQ(unmap.Post(TypeParam::Failure()), TypeParam::Failure());
    EXPECT_TRUE(this->ContainsMapping());
    unmap.Pre(this->address);
    EXPECT_EQ(unmap.Post(TypeParam::Success()), TypeParam::Success());
    EXPECT_FALSE(this->ContainsMapping());
}

TYPED_TEST(ProfiledMapping, MissingUnmapSymbolPreservesReplayMemory)
{
    typename TypeParam::Map map;
    map.Pre(this->address, 64, 0, nullptr, 0);
    map.Post(TypeParam::Success());
    MappingHook<typename TypeParam::Unmap> unmap(nullptr);
    EXPECT_NE(unmap.Call(this->address), TypeParam::Success());
    EXPECT_TRUE(this->ContainsMapping());
}
