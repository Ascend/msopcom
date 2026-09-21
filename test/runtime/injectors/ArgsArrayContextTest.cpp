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
#include <cstdint>
#include <cstring>
#include <vector>

#include "runtime/inject_helpers/ArgsArrayContext.h"

using namespace std;

namespace {
// 模拟静态插桩 kernel 的 meta 布局：3 个 8 字节参数，末参为 memInfo
// packed 总长 0x18，工具通过 API 读到的 paramCount=原始参数个数+1
constexpr size_t kParamCount = 3;
const vector<size_t> kOffsets = {0, 8, 16};
const vector<size_t> kSizes = {8, 8, 8};
constexpr size_t kPackedSize = 24;

uint64_t PackedValue(const ArgsArrayContextSP &ctx, size_t offset)
{
    return *reinterpret_cast<const uint64_t *>(static_cast<const uint8_t *>(ctx->GetPackedArgs()) + offset);
}
} // namespace

class ArgsArrayContextTest : public testing::Test {};

// 场景：静态插桩下工具读到的 paramCount=原始参数+1，
// 但调用方 args 数组仍是原始参数个数。Init 只应读前 (paramCount-1) 个，不越界读 args[2]。
// 若误读越界下标：读到野指针会把垃圾拷进末槽（末槽应保持 0），读到 nullptr 则直接失败。
TEST_F(ArgsArrayContextTest, test_Init_staticStub_metaExtraParam_expect_noOOBRead_lastSlotUntouched)
{
    uint64_t p0 = 0x1111111111111111ULL;
    uint64_t p1 = 0x2222222222222222ULL;
    // 精确分配 2 个元素的指针数组：越界读 args[2] 将落在堆上未初始化区域
    auto argsArr = static_cast<void **>(malloc(2 * sizeof(void *)));
    ASSERT_NE(argsArr, nullptr);
    argsArr[0] = &p0;
    argsArr[1] = &p1;

    ArgsArrayContextSP ctx = ArgsArrayContext::Create(argsArr, kParamCount, kOffsets, kSizes, true);
    EXPECT_TRUE(ctx != nullptr && ctx->HasArgsArray());
    EXPECT_EQ(ctx->GetPackedArgsSize(), kPackedSize);
    EXPECT_EQ(PackedValue(ctx, 0), p0);
    EXPECT_EQ(PackedValue(ctx, 8), p1);
    constexpr uint8_t zeros[8] = {0};
    EXPECT_EQ(memcmp(static_cast<const uint8_t *>(ctx->GetPackedArgs()) + 16, zeros, sizeof(zeros)), 0);

    free(argsArr);
}

// 场景：静态插桩下 ExpandArgs 不追加缓冲，而是把 memInfo 原位填进 meta 末参槽，
// paramOffset 返回该偏移，参数指针数组长度=paramCount 且末指针指向末槽。
TEST_F(ArgsArrayContextTest, test_ExpandArgs_staticStub_expect_fillLastSlotInPlace)
{
    uint64_t p0 = 0xAULL;
    uint64_t p1 = 0xBULL;
    void *argsArr[2] = {&p0, &p1};
    ArgsArrayContextSP ctx = ArgsArrayContext::Create(argsArr, kParamCount, kOffsets, kSizes, true);
    ASSERT_TRUE(ctx != nullptr);

    uint64_t memInfo = 0xC0DECAFEULL;
    uint32_t offset = 0;
    EXPECT_TRUE(ctx->ExpandArgs(&memInfo, sizeof(memInfo), offset));
    EXPECT_EQ(offset, 16U);                          // 落在 meta 末参槽位，而非尾部追加
    EXPECT_EQ(ctx->GetPackedArgsSize(), kPackedSize); // 缓冲不增长
    EXPECT_EQ(PackedValue(ctx, 16), memInfo);

    void **argsArray = ctx->GetArgsArray();
    EXPECT_EQ(argsArray[kParamCount - 1], static_cast<uint8_t *>(ctx->GetPackedArgs()) + 16);
    EXPECT_EQ(*static_cast<uint64_t *>(argsArray[0]), p0);
    EXPECT_EQ(*static_cast<uint64_t *>(argsArray[1]), p1);
}

// 静态插桩下 memInfo 大小超过末参槽容量 -> 返回 false，缓冲保持原样
TEST_F(ArgsArrayContextTest, test_ExpandArgs_staticStub_sizeExceedsLastSlot_expect_false)
{
    uint64_t p0 = 0xAULL;
    uint64_t p1 = 0xBULL;
    void *argsArr[2] = {&p0, &p1};
    ArgsArrayContextSP ctx = ArgsArrayContext::Create(argsArr, kParamCount, kOffsets, kSizes, true);
    ASSERT_TRUE(ctx != nullptr);

    uint64_t oversized[2] = {0};
    uint32_t offset = 0;
    EXPECT_FALSE(ctx->ExpandArgs(oversized, sizeof(oversized), offset));
    EXPECT_EQ(ctx->GetPackedArgsSize(), kPackedSize);
    constexpr uint8_t zeros[8] = {0};
    EXPECT_EQ(memcmp(static_cast<const uint8_t *>(ctx->GetPackedArgs()) + 16, zeros, sizeof(zeros)), 0);
}

// 动态插桩路径行为不变——ExpandArgs 在对齐处追加、缓冲增长、
// 参数指针数组长度=paramCount+1，追加前末指针为空、追加后指向 memInfo。
TEST_F(ArgsArrayContextTest, test_ExpandArgs_dynamicStub_expect_appendAligned)
{
    constexpr size_t kDynParamCount = 2;
    const vector<size_t> dynOffsets = {0, 8};
    const vector<size_t> dynSizes = {8, 8};
    uint64_t p0 = 0xAULL;
    uint64_t p1 = 0xBULL;
    void *argsArr[2] = {&p0, &p1};
    ArgsArrayContextSP ctx = ArgsArrayContext::Create(argsArr, kDynParamCount, dynOffsets, dynSizes);
    ASSERT_TRUE(ctx != nullptr);
    EXPECT_EQ(ctx->GetArgsArray()[kDynParamCount], nullptr);

    uint64_t memInfo = 0xC0DECAFEULL;
    uint32_t offset = 0;
    EXPECT_TRUE(ctx->ExpandArgs(&memInfo, sizeof(memInfo), offset));
    EXPECT_EQ(offset, 16U); // 8 对齐后的追加偏移
    EXPECT_EQ(ctx->GetPackedArgsSize(), kPackedSize);
    EXPECT_EQ(PackedValue(ctx, 16), memInfo);
    EXPECT_EQ(ctx->GetArgsArray()[kDynParamCount], static_cast<uint8_t *>(ctx->GetPackedArgs()) + 16);
}
