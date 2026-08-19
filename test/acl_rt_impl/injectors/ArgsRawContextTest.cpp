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
#include <cstdlib>
#include <vector>

#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/ArgsRawContext.h"
#include "runtime/inject_helpers/ArgsManager.h"

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
