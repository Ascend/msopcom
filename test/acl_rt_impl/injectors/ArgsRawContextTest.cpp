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

#include "runtime/inject_helpers/ArgsArrayContext.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <vector>

#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/ArgsRawContext.h"
#include "runtime/inject_helpers/ArgsManager.h"
#include "acl_rt_impl/AscendclImplOrigin.h"

using namespace std;

class ArgsRawContextTest : public testing::Test {
public:
    void SetUp() override {}

    void TearDown() override {
        GlobalMockObject::verify();
        ArgsManager::Instance().Clear();
    }
};

/**
 * | 用例集 | ArgsRawContextTest
 * |测试函数| ArgsRawContext::ArgsRawContext(void*, uint32_t, const std::vector<aclrtPlaceHolderInfo>&)
 * | 用例名 | host_args_buffer_reused_after_construction_expect_snapshot_stable
 * |用例描述| 图模式(sink)下调用方 hostArgs 缓冲被复用后，构造时深拷贝的快照仍稳定可读
 */
TEST_F(ArgsRawContextTest, host_args_buffer_reused_after_construction_expect_snapshot_stable) {
    // 模拟图框架的 hostArgs：调用时填充参数，launch 返回后被下一算子复用/覆盖
    constexpr size_t ARGS_SIZE = 32;
    std::vector<uint8_t> hostArgs(ARGS_SIZE);
    for (size_t i = 0; i < ARGS_SIZE; ++i) {
        hostArgs[i] = static_cast<uint8_t>(i);
    }
    std::vector<aclrtPlaceHolderInfo> placeHolders{{8, 16}};

    ArgsRawContext ctx(hostArgs.data(), ARGS_SIZE, placeHolders);

    // 构造时应深拷贝一份快照，args_ 不应再指向调用方缓冲
    ASSERT_EQ(ctx.GetArgsSize(), ARGS_SIZE);
    ASSERT_NE(ctx.GetArgs(), hostArgs.data());
    ASSERT_EQ(ctx.GetPlaceholderInfo().size(), placeHolders.size());
    ASSERT_EQ(ctx.GetPlaceholderInfo()[0].addrOffset, placeHolders[0].addrOffset);
    ASSERT_EQ(ctx.GetPlaceholderInfo()[0].dataOffset, placeHolders[0].dataOffset);

    // 模拟异步 dump 回调执行前，调用方缓冲已被覆盖
    for (size_t i = 0; i < ARGS_SIZE; ++i) {
        hostArgs[i] = 0xFF;
    }

    // dump 阶段读取 args_ 仍应读到 launch 时的稳定入参
    const auto *snapshot = static_cast<const uint8_t *>(ctx.GetArgs());
    ASSERT_NE(snapshot, nullptr);
    for (size_t i = 0; i < ARGS_SIZE; ++i) {
        ASSERT_EQ(snapshot[i], static_cast<uint8_t>(i));
    }
}

/**
 * | 用例集 | ArgsRawContextTest
 * |测试函数| ArgsRawContext::GetTilingData()
 * | 用例名 | get_tiling_data_read_snapshot_after_buffer_reused
 * |用例描述| hostArgs 缓冲复用后，dump 链路 GetTilingData() 仍从快照读到正确的 tilingData
 */
TEST_F(ArgsRawContextTest, get_tiling_data_read_snapshot_after_buffer_reused) {
    constexpr size_t ARGS_SIZE = 32;
    constexpr size_t TILING_SIZE = 8;
    std::vector<uint8_t> hostArgs(ARGS_SIZE);
    for (size_t i = 0; i < ARGS_SIZE; ++i) {
        hostArgs[i] = static_cast<uint8_t>(i);
    }
    // 最后一个 placeholder 的 dataOffset 处为 tilingData
    std::vector<aclrtPlaceHolderInfo> placeHolders{{16, 0}};

    ArgsRawContext ctx(hostArgs.data(), ARGS_SIZE, placeHolders);

    // 模拟调用方缓冲被复用覆盖
    for (size_t i = 0; i < ARGS_SIZE; ++i) {
        hostArgs[i] = 0xFF;
    }

    std::vector<uint8_t> tilingData;
    tilingData.resize(TILING_SIZE);
    ASSERT_TRUE(ctx.GetTilingData(tilingData));
    for (size_t i = 0; i < TILING_SIZE; ++i) {
        ASSERT_EQ(tilingData[i], static_cast<uint8_t>(i));
    }
}

/**
 * | 用例集 | ArgsRawContextTest
 * |测试函数| ArgsRawContext::Clone()
 * | 用例名 | clone_keeps_snapshot_after_buffer_reused
 * |用例描述| 克隆后的上下文在 hostArgs 缓冲复用后仍能读到稳定入参快照
 */
TEST_F(ArgsRawContextTest, clone_keeps_snapshot_after_buffer_reused) {
    constexpr size_t ARGS_SIZE = 32;
    std::vector<uint8_t> hostArgs(ARGS_SIZE);
    for (size_t i = 0; i < ARGS_SIZE; ++i) {
        hostArgs[i] = static_cast<uint8_t>(i);
    }
    std::vector<aclrtPlaceHolderInfo> placeHolders{{8, 16}};

    ArgsRawContext ctx(hostArgs.data(), ARGS_SIZE, placeHolders);
    for (size_t i = 0; i < ARGS_SIZE; ++i) {
        hostArgs[i] = 0xFF;
    }

    auto cloned = ctx.Clone();
    ASSERT_NE(cloned, nullptr);
    auto clonedRaw = std::static_pointer_cast<ArgsRawContext>(cloned);
    const auto *snapshot = static_cast<const uint8_t *>(clonedRaw->GetArgs());
    ASSERT_NE(snapshot, nullptr);
    for (size_t i = 0; i < ARGS_SIZE; ++i) {
        ASSERT_EQ(snapshot[i], static_cast<uint8_t>(i));
    }
}

namespace {
aclError CopyHostArgs(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind) {
    if ((kind != ACL_MEMCPY_HOST_TO_HOST && kind != ACL_MEMCPY_DEVICE_TO_HOST &&
         kind != ACL_MEMCPY_HOST_TO_DEVICE) || destMax < count) {
        return ACL_ERROR_INTERNAL_ERROR;
    }
    std::memcpy(dst, src, count);
    return ACL_SUCCESS;
}

aclError MallocHostArgs(void **ptr, size_t size) {
    *ptr = std::malloc(size);
    return *ptr == nullptr ? ACL_ERROR_BAD_ALLOC : ACL_SUCCESS;
}

aclError MallocDeviceArgs(void **ptr, size_t size, aclrtMemMallocPolicy) {
    return MallocHostArgs(ptr, size);
}

aclError FreeArgsBuffer(void *ptr) {
    std::free(ptr);
    return ACL_SUCCESS;
}
}

TEST_F(ArgsRawContextTest, expanding_snapshot_keeps_tiling_data_and_updates_placeholder_offset) {
    std::vector<uint8_t> hostArgs(32);
    std::iota(hostArgs.begin(), hostArgs.end(), uint8_t{0});
    ArgsRawContext ctx(hostArgs.data(), hostArgs.size(), {{8, 16}});
    MOCKER(aclrtMemcpyImplOrigin).stubs().will(invoke(CopyHostArgs));
    uint64_t memInfo = 0x1122334455667788ULL;
    uint32_t offset = 0;
    ASSERT_TRUE(ctx.ExpandArgs(&memInfo, sizeof(memInfo), offset));
    EXPECT_EQ(offset, 16U);
    EXPECT_EQ(ctx.GetLastParamOffset(), 16U);
    EXPECT_EQ(ctx.GetArgsSize(), hostArgs.size() + sizeof(memInfo));
    EXPECT_EQ(ctx.GetPlaceholderInfo().at(0).dataOffset, 24U);
    const auto *expanded = static_cast<const uint8_t *>(ctx.GetArgs());
    ASSERT_NE(expanded, nullptr);
    EXPECT_EQ(std::memcmp(expanded + offset, &memInfo, sizeof(memInfo)), 0);
    std::vector<uint8_t> tilingData(2);
    ASSERT_TRUE(ctx.GetTilingData(tilingData));
    EXPECT_EQ(tilingData, (std::vector<uint8_t>{16, 17}));
}

TEST_F(ArgsRawContextTest, expanding_plain_host_args_appends_aligned_metadata) {
    std::vector<uint8_t> hostArgs{1, 2, 3, 4, 5};
    ArgsRawContext ctx(hostArgs.data(), hostArgs.size());
    MOCKER(aclrtMemcpyImplOrigin).stubs().will(invoke(CopyHostArgs));
    uint64_t memInfo = 0x1234;
    uint32_t offset = 0;
    ASSERT_TRUE(ctx.ExpandArgs(&memInfo, sizeof(memInfo), offset));
    EXPECT_EQ(offset, 8U);
    EXPECT_EQ(ctx.GetArgsSize(), 16U);
    const auto *expanded = static_cast<const uint8_t *>(ctx.GetArgs());
    EXPECT_EQ(std::memcmp(expanded, hostArgs.data(), hostArgs.size()), 0);
    EXPECT_EQ(std::memcmp(expanded + offset, &memInfo, sizeof(memInfo)), 0);
}

TEST_F(ArgsRawContextTest, saving_device_args_reads_input_address_into_dump_metadata) {
    uint64_t deviceArgs[] = {0x12345678ULL};
    ArgsRawContext ctx(deviceArgs, sizeof(deviceArgs), true);
    MOCKER(aclrtMallocHostImplOrigin).stubs().will(invoke(MallocHostArgs));
    MOCKER(aclrtMemcpyImplOrigin).stubs().will(invoke(CopyHostArgs));
    MOCKER(aclrtFreeHostImplOrigin).stubs().will(invoke(FreeArgsBuffer));
    OpMemInfo memInfo;
    memInfo.inputParamsAddrInfos.push_back({0, 0});
    DumperContext dump;
    ASSERT_TRUE(ctx.Save(".", dump, memInfo, false));
    EXPECT_EQ(memInfo.inputParamsAddrInfos[0].addr, deviceArgs[0]);
    EXPECT_EQ(dump.inputPathList, (std::vector<std::string>{"n"}));
    EXPECT_EQ(dump.inputSizeList, (std::vector<uint64_t>{0}));
}

TEST_F(ArgsRawContextTest, expanding_device_args_copies_payload_and_metadata_back_to_device) {
    uint64_t original = 0x10203040ULL;
    uint64_t memInfo = 0x55667788ULL;
    ArgsRawContext ctx(&original, sizeof(original), true);
    MOCKER(aclrtMemcpyImplOrigin).stubs().will(invoke(CopyHostArgs));
    MOCKER(aclrtMallocImplOrigin).stubs().will(invoke(MallocDeviceArgs));
    MOCKER(aclrtFreeImplOrigin).stubs().will(invoke(FreeArgsBuffer));
    uint32_t offset = 0;
    ASSERT_TRUE(ctx.ExpandArgs(&memInfo, sizeof(memInfo), offset));
    ASSERT_NE(ctx.GetArgs(), &original);
    EXPECT_EQ(offset, sizeof(original));
    EXPECT_EQ(ctx.GetArgsSize(), sizeof(original) + sizeof(memInfo));
    uint64_t copiedOriginal = 0;
    uint64_t copiedMetadata = 0;
    std::memcpy(&copiedOriginal, ctx.GetArgs(), sizeof(copiedOriginal));
    std::memcpy(&copiedMetadata, static_cast<uint8_t *>(ctx.GetArgs()) + offset, sizeof(copiedMetadata));
    EXPECT_EQ(copiedOriginal, original);
    EXPECT_EQ(copiedMetadata, memInfo);
}

TEST(ArgsArrayContextCoverageTest, clone_appends_metadata_without_changing_original_launch_args) {
    uint64_t first = 0x1122;
    uint32_t second = 0x3344;
    void *args[] = {&first, &second};
    auto original = ArgsArrayContext::Create(args, 2, {0, 16}, {sizeof(first), sizeof(second)});
    ASSERT_NE(original, nullptr);
    ASSERT_EQ(original->GetPackedArgsSize(), 20U);
    EXPECT_EQ(*static_cast<uint64_t *>(original->GetArgsArray()[0]), first);
    EXPECT_EQ(*static_cast<uint32_t *>(original->GetArgsArray()[1]), second);
    auto expanded = std::static_pointer_cast<ArgsArrayContext>(original->Clone());
    ASSERT_NE(expanded, nullptr);
    uint64_t memInfo = 0x5566;
    uint32_t offset = 0;
    ASSERT_TRUE(expanded->ExpandArgs(&memInfo, sizeof(memInfo), offset));
    EXPECT_EQ(offset, 24U);
    EXPECT_EQ(expanded->GetPackedArgsSize(), 32U);
    EXPECT_EQ(*static_cast<uint64_t *>(expanded->GetArgsArray()[2]), memInfo);
    EXPECT_EQ(original->GetPackedArgsSize(), 20U);
    EXPECT_EQ(original->GetArgsArray()[2], nullptr);
    EXPECT_NE(expanded->GetArgsArray()[0], original->GetArgsArray()[0]);
}

TEST(ArgsArrayContextCoverageTest, save_uses_packed_input_address_and_rejects_out_of_range_metadata) {
    uint64_t inputAddress = 0x12345678;
    void *args[] = {&inputAddress};
    auto context = ArgsArrayContext::Create(args, 1, {0}, {sizeof(inputAddress)});
    ASSERT_NE(context, nullptr);
    OpMemInfo memInfo;
    memInfo.inputParamsAddrInfos.push_back({0, 0});
    DumperContext dump;
    ASSERT_TRUE(context->Save(".", dump, memInfo, false));
    EXPECT_EQ(memInfo.inputParamsAddrInfos[0].addr, inputAddress);
    EXPECT_EQ(dump.inputPathList, (std::vector<std::string>{"n"}));
    memInfo.skipNum = 1;
    EXPECT_FALSE(context->Save(".", dump, memInfo, false));
}

TEST(ArgsArrayContextCoverageTest, invalid_layout_does_not_create_launch_context) {
    uint32_t value = 1;
    void *args[] = {&value};
    EXPECT_EQ(ArgsArrayContext::Create(nullptr, 1, {0}, {sizeof(value)}), nullptr);
    EXPECT_EQ(ArgsArrayContext::Create(args, 2, {0}, {sizeof(value)}), nullptr);
    auto shortParam = ArgsArrayContext::Create(args, 1, {0}, {sizeof(value)});
    ASSERT_NE(shortParam, nullptr);
    OpMemInfo memInfo;
    memInfo.inputParamsAddrInfos.push_back({0, 0});
    DumperContext dump;
    EXPECT_FALSE(shortParam->Save(".", dump, memInfo, false));
}
