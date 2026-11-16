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


#define private public
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/DeviceContext.h"
#undef private

// 该头会间接引入标准库 <sstream>，不能放在 #define private public 区间内
#include "runtime/inject_helpers/DbiRecordTaskHelper.h"

#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include "mockcpp/mockcpp.hpp"
#include "acl_rt_impl/AscendclImplOrigin.h"

namespace {
// RecordHeader(8B) + LoadStoreRecord(64B)，主机侧只按字节搬运，不关心具体内容
constexpr uint64_t TEST_RECORD_BYTES = 72U;
constexpr uint64_t END_FLAG_MESSAGE_NUM = 1U;
// 两档布局标识：0 表示确认纯 SIMD（整块单槽），MAX_THREAD_NUM 表示含 SIMT 或识别不确定（2049 槽）
constexpr uint64_t LAYOUT_PURE_SIMD = 0U;
constexpr uint64_t LAYOUT_WITH_SIMT = MAX_THREAD_NUM;
// 用例按 A5（带 SIMT 的芯片）配置：每个 block 200MB
constexpr uint64_t TEST_BLOCK_DATA_SIZE = MC_BLOCK_DATA_SIZE_SIMT;
constexpr uint64_t TEST_BLOCK_MEM_SIZE = TEST_BLOCK_DATA_SIZE + BLOCK_GAP;
constexpr uint64_t TEST_SEGMENT_AREA = TEST_BLOCK_DATA_SIZE - sizeof(BlockHeader);

std::function<void(uint8_t *)> gBlockWriter;
std::vector<std::string> gSentMessages;
std::vector<uint64_t> gWrittenLayoutIds;
bool gMemcpyFails = false;

aclError WriteBlockMemory(void *dst, size_t, const void *src, size_t count, aclrtMemcpyKind kind)
{
    if (gMemcpyFails) {
        return ACL_ERROR_BAD_ALLOC;
    }
    if (kind == ACL_MEMCPY_HOST_TO_DEVICE && count == sizeof(MemoryChartLayoutHeader)) {
        MemoryChartLayoutHeader layoutHeader{};
        std::memcpy(&layoutHeader, src, sizeof(layoutHeader));
        gWrittenLayoutIds.push_back(layoutHeader.layoutId);
        std::memcpy(dst, src, sizeof(layoutHeader));
        return ACL_SUCCESS;
    }
    if (gBlockWriter != nullptr) {
        gBlockWriter(static_cast<uint8_t *>(dst));
    }
    return ACL_SUCCESS;
}

void CaptureSentMessage(ProfConfig *, const std::string &msg)
{
    gSentMessages.push_back(msg);
}

void WriteSegment(uint8_t *block, uint64_t layoutId, uint64_t segIdx, uint64_t count, uint64_t length,
                  uint64_t dropped, uint8_t fill, bool fillPayload = true, uint64_t segmentArea = TEST_SEGMENT_AREA)
{
    const uint64_t offset = MemoryChartSegmentOffsetInBlock(segmentArea, segIdx, layoutId);
    auto *segHeader = reinterpret_cast<MemoryChartSegmentHeader *>(block + offset);
    segHeader->count = count;
    segHeader->length = length;
    segHeader->dropped = dropped;
    if (fillPayload && length != 0 && length <= MemoryChartSegmentCapacity(segmentArea, layoutId)) {
        std::memset(block + offset + MC_SEGMENT_HEADER_SIZE, fill, static_cast<size_t>(length));
    }
}

void SetBlockLayout(uint8_t *block, uint64_t layoutId)
{
    auto *header = reinterpret_cast<BlockHeader *>(block);
    header->threadNum = layoutId;
    header->length = MC_LAYOUT_TAG;
}

DBIDataHeader GetSentHeader(const std::string &msg)
{
    DBIDataHeader header{};
    if (msg.size() >= sizeof(ProfPacketHead) + sizeof(DBIDataHeader)) {
        std::memcpy(&header, msg.data() + sizeof(ProfPacketHead), sizeof(header));
    }
    return header;
}

std::string GetSentPayload(const std::string &msg)
{
    const uint64_t offset = sizeof(ProfPacketHead) + sizeof(DBIDataHeader);
    return msg.size() > offset ? msg.substr(offset) : std::string();
}
} // namespace

class MemoryChartSegmentTest : public testing::Test {
public:
    void SetUp() override
    {
        gBlockWriter = nullptr;
        gMemcpyFails = false;
        gSentMessages.clear();
        gWrittenLayoutIds.clear();
        DeviceContext::Local().SetSocVersion("Ascend950PR");
        ProfConfig::Instance().profConfig_.isSimulator = false;
        MOCKER(&ProfConfig::SendMsg).stubs().will(invoke(CaptureSentMessage));
        MOCKER(aclrtMemcpyImplOrigin).stubs().will(invoke(WriteBlockMemory));
    }

    void TearDown() override
    {
        gBlockWriter = nullptr;
        gSentMessages.clear();
        gWrittenLayoutIds.clear();
        DeviceContext::Local().SetSocVersion("");
        ProfConfig::Instance().Reset();
        GlobalMockObject::verify();
    }

    void DumpOnce(uint64_t memSize = TEST_BLOCK_MEM_SIZE)
    {
        ProfDataCollect collector(nullptr, false);
        collector.GenDBIData(memSize, nullptr);
    }
};

// 确认纯 SIMD（布局标识 0）只有一个 0 号槽，独占整个 block，容量等于整块可用空间（MemoryChartSegmentNum/Stride/OffsetInBlock/Capacity）
TEST_F(MemoryChartSegmentTest, layout_pure_simd_uses_whole_block)
{
    EXPECT_TRUE(MemoryChartLayoutIsValid(LAYOUT_PURE_SIMD));
    EXPECT_EQ(MemoryChartSegmentNum(LAYOUT_PURE_SIMD), 1U);
    const uint64_t stride = MemoryChartSegmentStride(TEST_SEGMENT_AREA, LAYOUT_PURE_SIMD);
    EXPECT_EQ(stride, TEST_SEGMENT_AREA & ~0x7ULL);
    EXPECT_EQ(stride % 8U, 0U);
    EXPECT_EQ(MemoryChartSegmentOffsetInBlock(TEST_SEGMENT_AREA, 0, LAYOUT_PURE_SIMD), sizeof(BlockHeader));
    EXPECT_EQ(MemoryChartSegmentCapacity(TEST_SEGMENT_AREA, LAYOUT_PURE_SIMD), stride - MC_SEGMENT_HEADER_SIZE);
    EXPECT_GE(MemoryChartSegmentCapacity(TEST_SEGMENT_AREA, LAYOUT_PURE_SIMD), TEST_RECORD_BYTES);
    EXPECT_LE(sizeof(BlockHeader) + stride, TEST_BLOCK_DATA_SIZE);
}

// 含 SIMT 时固定 2049 个槽，步长与槽位数无关于实际线程数，所有槽都在 block 内（MemoryChartSegmentNum/Stride）
TEST_F(MemoryChartSegmentTest, layout_with_simt_has_fixed_slots)
{
    EXPECT_TRUE(MemoryChartLayoutIsValid(LAYOUT_WITH_SIMT));
    EXPECT_EQ(MemoryChartSegmentNum(LAYOUT_WITH_SIMT), MAX_THREAD_NUM + 1U);
    const uint64_t stride = MemoryChartSegmentStride(TEST_SEGMENT_AREA, LAYOUT_WITH_SIMT);
    EXPECT_EQ(stride % 8U, 0U);
    EXPECT_EQ(stride, (TEST_SEGMENT_AREA / (MAX_THREAD_NUM + 1U)) & ~0x7ULL);
    EXPECT_EQ(MemoryChartSegmentOffsetInBlock(TEST_SEGMENT_AREA, 0, LAYOUT_WITH_SIMT), sizeof(BlockHeader));
    // 最后一个槽也不能越出 block
    const uint64_t lastEnd = MemoryChartSegmentOffsetInBlock(TEST_SEGMENT_AREA, MAX_THREAD_NUM, LAYOUT_WITH_SIMT) +
        stride;
    EXPECT_LE(lastEnd, TEST_BLOCK_DATA_SIZE);
    EXPECT_GE(MemoryChartSegmentCapacity(TEST_SEGMENT_AREA, LAYOUT_WITH_SIMT), TEST_RECORD_BYTES);
}

// 0 号槽预留给 SIMD，SIMT 线程占 1..MAX_THREAD_NUM 号槽；槽位连续不重叠（MemoryChartSegmentOffsetInBlock）
TEST_F(MemoryChartSegmentTest, layout_simt_segments_are_disjoint_and_ordered)
{
    const uint64_t stride = MemoryChartSegmentStride(TEST_SEGMENT_AREA, LAYOUT_WITH_SIMT);
    for (uint64_t seg = 0; seg <= MAX_THREAD_NUM; ++seg) {
        const uint64_t offset = MemoryChartSegmentOffsetInBlock(TEST_SEGMENT_AREA, seg, LAYOUT_WITH_SIMT);
        EXPECT_EQ(offset, sizeof(BlockHeader) + seg * stride);
        EXPECT_LE(offset + stride, TEST_BLOCK_DATA_SIZE);
        if (seg != 0) {
            EXPECT_EQ(offset,
                MemoryChartSegmentOffsetInBlock(TEST_SEGMENT_AREA, seg - 1, LAYOUT_WITH_SIMT) + stride);
        }
    }
}

// 确认纯 SIMD 时写入整块单槽布局，并同时写入布局魔数供回收时校验（ProfDataCollect::InitMemoryChartLayout）
TEST_F(MemoryChartSegmentTest, init_layout_pure_simd_keeps_whole_block)
{
    std::vector<uint8_t> memInfo(TEST_BLOCK_MEM_SIZE, 0);
    ProfDataCollect collector(nullptr, false);
    EXPECT_TRUE(collector.InitMemoryChartLayout(memInfo.data(), memInfo.size(), true, false));
    ASSERT_EQ(gWrittenLayoutIds.size(), 1U);
    EXPECT_EQ(gWrittenLayoutIds[0], LAYOUT_PURE_SIMD);
    const auto *blockHeader = reinterpret_cast<const BlockHeader *>(memInfo.data());
    EXPECT_EQ(blockHeader->threadNum, LAYOUT_PURE_SIMD);
    EXPECT_EQ(blockHeader->length, MC_LAYOUT_TAG);
}

// 含 SIMT 时为每个 block 写入固定布局标识，尾部 gap 不受影响（ProfDataCollect::InitMemoryChartLayout）
TEST_F(MemoryChartSegmentTest, init_layout_with_simt_writes_every_block)
{
    constexpr uint64_t blockNum = 2U;
    std::vector<uint8_t> memInfo(TEST_BLOCK_MEM_SIZE * blockNum, 0);
    ProfDataCollect collector(nullptr, false);
    EXPECT_TRUE(collector.InitMemoryChartLayout(memInfo.data(), memInfo.size(), true, true));
    ASSERT_EQ(gWrittenLayoutIds.size(), blockNum);
    for (uint64_t layoutId : gWrittenLayoutIds) {
        EXPECT_EQ(layoutId, LAYOUT_WITH_SIMT);
    }
    for (uint64_t i = 0; i < blockNum; ++i) {
        EXPECT_EQ(reinterpret_cast<BlockHeader *>(memInfo.data() + i * TEST_BLOCK_MEM_SIZE)->threadNum,
            LAYOUT_WITH_SIMT);
    }
}

// 二进制扫描失败（识别不确定）时按含 SIMT 保守分槽，避免 SIMT 记录越界（ProfDataCollect::InitMemoryChartLayout）
TEST_F(MemoryChartSegmentTest, init_layout_scan_unknown_uses_conservative_layout)
{
    std::vector<uint8_t> memInfo(TEST_BLOCK_MEM_SIZE, 0);
    ProfDataCollect collector(nullptr, false);
    EXPECT_TRUE(collector.InitMemoryChartLayout(memInfo.data(), memInfo.size(), false, false));
    ASSERT_EQ(gWrittenLayoutIds.size(), 1U);
    EXPECT_EQ(gWrittenLayoutIds[0], LAYOUT_WITH_SIMT);
    EXPECT_EQ(reinterpret_cast<BlockHeader *>(memInfo.data())->threadNum, LAYOUT_WITH_SIMT);
}

// A2/A3 没有 SIMT 硬件，无论识别成功与否都使用整块单槽，不能因为识别失败退化到 2049 槽（ProfDataCollect::InitMemoryChartLayout）
TEST_F(MemoryChartSegmentTest, init_layout_a2a3_always_uses_whole_block)
{
    DeviceContext::Local().SetSocVersion("Ascend910B4");
    std::vector<uint8_t> memInfo(MC_BLOCK_DATA_SIZE_SIMD + BLOCK_GAP, 0);
    ProfDataCollect collector(nullptr, false);
    EXPECT_TRUE(collector.InitMemoryChartLayout(memInfo.data(), memInfo.size(), false, false));
    ASSERT_EQ(gWrittenLayoutIds.size(), 1U);
    EXPECT_EQ(gWrittenLayoutIds[0], LAYOUT_PURE_SIMD);
    const auto *blockHeader = reinterpret_cast<const BlockHeader *>(memInfo.data());
    EXPECT_EQ(blockHeader->threadNum, LAYOUT_PURE_SIMD);
    EXPECT_EQ(blockHeader->length, MC_LAYOUT_TAG);
}

// 缓冲区大小与 block 大小不匹配时初始化失败，调用方需终止本次回放（ProfDataCollect::InitMemoryChartLayout）
TEST_F(MemoryChartSegmentTest, init_layout_invalid_mem_size_fails)
{
    std::vector<uint8_t> memInfo(TEST_BLOCK_MEM_SIZE + 8, 0);
    ProfDataCollect collector(nullptr, false);
    EXPECT_FALSE(collector.InitMemoryChartLayout(memInfo.data(), memInfo.size(), true, true));
    EXPECT_TRUE(gWrittenLayoutIds.empty());
    EXPECT_FALSE(collector.InitMemoryChartLayout(memInfo.data(), 0, true, true));
}

// 预写布局标识失败时返回 false，调用方需终止本次回放（ProfDataCollect::InitMemoryChartLayout）
TEST_F(MemoryChartSegmentTest, init_layout_memcpy_failure_fails)
{
    std::vector<uint8_t> memInfo(TEST_BLOCK_MEM_SIZE, 0);
    gMemcpyFails = true;
    ProfDataCollect collector(nullptr, false);
    EXPECT_FALSE(collector.InitMemoryChartLayout(memInfo.data(), memInfo.size(), true, true));
}

// 纯 SIMD 单槽的记录被拼成一个数据包，count/length/blockId 正确（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, simd_block_is_packed_into_one_packet)
{
    gBlockWriter = [](uint8_t *block) {
        SetBlockLayout(block, LAYOUT_PURE_SIMD);
        WriteSegment(block, LAYOUT_PURE_SIMD, 0, 2, 2 * TEST_RECORD_BYTES, 0, 'A');
    };
    DumpOnce();
    ASSERT_EQ(gSentMessages.size(), 2U); // 1 个数据包 + 1 个结束包
    const auto header = GetSentHeader(gSentMessages[0]);
    EXPECT_EQ(header.count, 2U);
    EXPECT_EQ(header.length, 2 * TEST_RECORD_BYTES);
    EXPECT_EQ(header.overflow, 0U);
    EXPECT_EQ(header.blockId, 0U);
    EXPECT_EQ(header.endFlag, 0U);
    EXPECT_EQ(GetSentPayload(gSentMessages[0]), std::string(2 * TEST_RECORD_BYTES, 'A'));
    EXPECT_EQ(GetSentHeader(gSentMessages[1]).endFlag, 1U);
}

// 纯 SIMD 独占整块后容量不再受 1/2049 限制，2000 条记录全部上报（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, pure_simd_can_use_more_than_fixed_slot_capacity)
{
    constexpr uint64_t recordCount = 2000U; // 2000 * 72B 超过 200MB/2049 的单槽容量
    gBlockWriter = [](uint8_t *block) {
        SetBlockLayout(block, LAYOUT_PURE_SIMD);
        WriteSegment(block, LAYOUT_PURE_SIMD, 0, recordCount, recordCount * TEST_RECORD_BYTES, 0, 'A');
    };
    DumpOnce();
    ASSERT_EQ(gSentMessages.size(), 2U);
    const auto header = GetSentHeader(gSentMessages[0]);
    EXPECT_EQ(header.count, recordCount);
    EXPECT_EQ(header.length, recordCount * TEST_RECORD_BYTES);
    EXPECT_EQ(header.overflow, 0U);
    EXPECT_EQ(GetSentPayload(gSentMessages[0]), std::string(recordCount * TEST_RECORD_BYTES, 'A'));
}

// SIMT 线程记录写在第 1..N 号槽，按槽位顺序拼接，count 为各槽之和（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, simt_segments_are_concatenated_in_segment_order)
{
    gBlockWriter = [](uint8_t *block) {
        SetBlockLayout(block, LAYOUT_WITH_SIMT);
        WriteSegment(block, LAYOUT_WITH_SIMT, 1, 1, TEST_RECORD_BYTES, 0, 'A'); // thread 0
        WriteSegment(block, LAYOUT_WITH_SIMT, 2, 2, 2 * TEST_RECORD_BYTES, 0, 'B'); // thread 1
    };
    DumpOnce();
    ASSERT_EQ(gSentMessages.size(), 2U);
    const auto header = GetSentHeader(gSentMessages[0]);
    EXPECT_EQ(header.count, 3U);
    EXPECT_EQ(header.length, 3 * TEST_RECORD_BYTES);
    EXPECT_EQ(header.overflow, 0U);
    const std::string expected = std::string(TEST_RECORD_BYTES, 'A') + std::string(2 * TEST_RECORD_BYTES, 'B');
    EXPECT_EQ(GetSentPayload(gSentMessages[0]), expected);
}

// 混合内核里 SIMD 记录占 0 号槽、SIMT 线程占 1..N 号槽，两类记录都要完整上报（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, mixed_simd_and_simt_records_do_not_share_segment)
{
    gBlockWriter = [](uint8_t *block) {
        SetBlockLayout(block, LAYOUT_WITH_SIMT);
        WriteSegment(block, LAYOUT_WITH_SIMT, 0, 1, TEST_RECORD_BYTES, 0, 'S'); // SIMD
        WriteSegment(block, LAYOUT_WITH_SIMT, 1, 1, TEST_RECORD_BYTES, 0, 'A'); // thread 0
        WriteSegment(block, LAYOUT_WITH_SIMT, 2, 1, TEST_RECORD_BYTES, 0, 'B'); // thread 1
    };
    DumpOnce();
    ASSERT_EQ(gSentMessages.size(), 2U);
    const auto header = GetSentHeader(gSentMessages[0]);
    EXPECT_EQ(header.count, 3U);
    EXPECT_EQ(header.length, 3 * TEST_RECORD_BYTES);
    const std::string expected = std::string(TEST_RECORD_BYTES, 'S') + std::string(TEST_RECORD_BYTES, 'A') +
        std::string(TEST_RECORD_BYTES, 'B');
    EXPECT_EQ(GetSentPayload(gSentMessages[0]), expected);
}

// SIMT 线程 1 先写、SIMD 随后写，两类记录落在不同槽位，host 必须收集到 2 条（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, simt_then_simd_records_are_both_reported)
{
    gBlockWriter = [](uint8_t *block) {
        SetBlockLayout(block, LAYOUT_WITH_SIMT);
        WriteSegment(block, LAYOUT_WITH_SIMT, 2, 1, TEST_RECORD_BYTES, 0, 'B'); // SIMT 线程 1 先写
        WriteSegment(block, LAYOUT_WITH_SIMT, 0, 1, TEST_RECORD_BYTES, 0, 'S'); // SIMD 随后写
    };
    DumpOnce();
    ASSERT_EQ(gSentMessages.size(), 2U);
    const auto header = GetSentHeader(gSentMessages[0]);
    EXPECT_EQ(header.count, 2U);
    EXPECT_EQ(header.length, 2 * TEST_RECORD_BYTES);
    EXPECT_EQ(GetSentPayload(gSentMessages[0]),
        std::string(TEST_RECORD_BYTES, 'S') + std::string(TEST_RECORD_BYTES, 'B'));
}

// 布局只由 host 预写的布局标识决定：同一 block 内不同区域先后追加到同一槽位，2 条记录都要上报（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, header_layout_id_fixes_layout_for_whole_launch)
{
    gBlockWriter = [](uint8_t *block) {
        // 布局标识由 host 在 launch 前写入，设备侧只读不改
        SetBlockLayout(block, LAYOUT_WITH_SIMT);
        WriteSegment(block, LAYOUT_WITH_SIMT, 1, 1, TEST_RECORD_BYTES, 0, 'A');
        // 后续区域在同一槽位追加 1 条，起点不变，不清空已有记录
        WriteSegment(block, LAYOUT_WITH_SIMT, 1, 2, 2 * TEST_RECORD_BYTES, 0, 'B', false);
        std::memset(block + MemoryChartSegmentOffsetInBlock(TEST_SEGMENT_AREA, 1, LAYOUT_WITH_SIMT) +
            MC_SEGMENT_HEADER_SIZE + TEST_RECORD_BYTES, 'B', static_cast<size_t>(TEST_RECORD_BYTES));
    };
    DumpOnce();
    ASSERT_EQ(gSentMessages.size(), 2U);
    const auto header = GetSentHeader(gSentMessages[0]);
    EXPECT_EQ(header.count, 2U);
    EXPECT_EQ(header.length, 2 * TEST_RECORD_BYTES);
    EXPECT_EQ(GetSentPayload(gSentMessages[0]),
        std::string(TEST_RECORD_BYTES, 'A') + std::string(TEST_RECORD_BYTES, 'B'));
}

// 各槽丢弃的记录数累加进 overflow，即使没有有效记录也要上报（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, dropped_records_are_reported_as_overflow)
{
    gBlockWriter = [](uint8_t *block) {
        SetBlockLayout(block, LAYOUT_WITH_SIMT);
        WriteSegment(block, LAYOUT_WITH_SIMT, 1, 1, TEST_RECORD_BYTES, 3, 'A');
        WriteSegment(block, LAYOUT_WITH_SIMT, 2, 0, 0, 4, 'B');
    };
    DumpOnce();
    ASSERT_EQ(gSentMessages.size(), 2U);
    const auto header = GetSentHeader(gSentMessages[0]);
    EXPECT_EQ(header.count, 1U);
    EXPECT_EQ(header.overflow, 7U);
}

// 布局标识不是两档合法值时整块数据跳过，只发送结束包（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, invalid_layout_id_skips_block)
{
    gBlockWriter = [](uint8_t *block) {
        SetBlockLayout(block, 1024U); // 旧的动态线程数写法，已不再合法
        WriteSegment(block, 0, 0, 1, TEST_RECORD_BYTES, 0, 'A');
    };
    DumpOnce();
    EXPECT_FALSE(MemoryChartLayoutIsValid(1024U));
    ASSERT_EQ(gSentMessages.size(), END_FLAG_MESSAGE_NUM);
    EXPECT_EQ(GetSentHeader(gSentMessages[0]).endFlag, 1U);
}

// 布局标识合法但魔数不匹配（host 与设备桩布局口径不一致）时整块丢弃，只发送结束包（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, layout_tag_mismatch_skips_block)
{
    gBlockWriter = [](uint8_t *block) {
        reinterpret_cast<BlockHeader *>(block)->threadNum = LAYOUT_PURE_SIMD;
        // 不写 MC_LAYOUT_TAG，模拟混版时旧格式数据被当成分区解析
        WriteSegment(block, LAYOUT_PURE_SIMD, 0, 1, TEST_RECORD_BYTES, 0, 'A');
    };
    DumpOnce();
    ASSERT_EQ(gSentMessages.size(), END_FLAG_MESSAGE_NUM);
    EXPECT_EQ(GetSentHeader(gSentMessages[0]).endFlag, 1U);
}

// 槽 length 超过容量时该槽被跳过，其余槽正常拼接（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, segment_length_exceed_capacity_is_skipped)
{
    gBlockWriter = [](uint8_t *block) {
        SetBlockLayout(block, LAYOUT_WITH_SIMT);
        WriteSegment(block, LAYOUT_WITH_SIMT, 1, 1, TEST_RECORD_BYTES, 0, 'A');
        const uint64_t capacity = MemoryChartSegmentCapacity(TEST_SEGMENT_AREA, LAYOUT_WITH_SIMT);
        WriteSegment(block, LAYOUT_WITH_SIMT, 2, 1, capacity + 8, 0, 'B', false);
    };
    DumpOnce();
    ASSERT_EQ(gSentMessages.size(), 2U);
    const auto header = GetSentHeader(gSentMessages[0]);
    EXPECT_EQ(header.count, 1U);
    EXPECT_EQ(header.length, TEST_RECORD_BYTES);
    EXPECT_EQ(GetSentPayload(gSentMessages[0]), std::string(TEST_RECORD_BYTES, 'A'));
}

// 槽 count 与 length 不自洽（count > length/8）时该槽被跳过（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, segment_count_larger_than_length_is_skipped)
{
    gBlockWriter = [](uint8_t *block) {
        SetBlockLayout(block, LAYOUT_PURE_SIMD);
        WriteSegment(block, LAYOUT_PURE_SIMD, 0, 10, TEST_RECORD_BYTES, 0, 'A');
    };
    DumpOnce();
    ASSERT_EQ(gSentMessages.size(), END_FLAG_MESSAGE_NUM);
    EXPECT_EQ(GetSentHeader(gSentMessages[0]).endFlag, 1U);
}

// 分区头 count 为 0 但 length 非 0（部分写入或损坏）时按损坏分区报错，不能当作空分区静默跳过（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, segment_count_zero_with_length_is_reported)
{
    gBlockWriter = [](uint8_t *block) {
        SetBlockLayout(block, LAYOUT_PURE_SIMD);
        WriteSegment(block, LAYOUT_PURE_SIMD, 0, 0, TEST_RECORD_BYTES, 0, 'A');
    };
    DumpOnce();
    // 损坏分区不产生数据包，但必须有结束包，避免静默丢失
    ASSERT_EQ(gSentMessages.size(), END_FLAG_MESSAGE_NUM);
    EXPECT_EQ(GetSentHeader(gSentMessages[0]).endFlag, 1U);
}

// block 内没有任何记录且没有丢弃时不上报数据包（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, empty_block_sends_only_end_flag)
{
    gBlockWriter = [](uint8_t *block) { SetBlockLayout(block, LAYOUT_PURE_SIMD); };
    DumpOnce();
    ASSERT_EQ(gSentMessages.size(), END_FLAG_MESSAGE_NUM);
    EXPECT_EQ(GetSentHeader(gSentMessages[0]).endFlag, 1U);
}

// 设备到主机拷贝失败时不再上报该 block 的数据，只发送结束包（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, memcpy_failure_skips_block)
{
    gMemcpyFails = true;
    DumpOnce();
    ASSERT_EQ(gSentMessages.size(), END_FLAG_MESSAGE_NUM);
    EXPECT_EQ(GetSentHeader(gSentMessages[0]).endFlag, 1U);
}

// memSize 为 0 时直接返回，不发送任何消息（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, zero_mem_size_sends_nothing)
{
    DumpOnce(0);
    EXPECT_TRUE(gSentMessages.empty());
}

// 记录长度随指令类型变化（如 Load2D 为 52B，不是 8 的整数倍），回收侧不能按 8 字节整除拒收（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, simd_52_byte_record_is_accepted)
{
    constexpr uint64_t recordBytes = 52U;
    ASSERT_NE(recordBytes % 8U, 0U);
    gBlockWriter = [](uint8_t *block) {
        SetBlockLayout(block, LAYOUT_PURE_SIMD);
        WriteSegment(block, LAYOUT_PURE_SIMD, 0, 1, recordBytes, 0, 'C');
    };
    DumpOnce();
    ASSERT_EQ(gSentMessages.size(), 2U);
    const auto header = GetSentHeader(gSentMessages[0]);
    EXPECT_EQ(header.count, 1U);
    EXPECT_EQ(header.length, recordBytes);
    EXPECT_EQ(header.overflow, 0U);
    EXPECT_EQ(GetSentPayload(gSentMessages[0]), std::string(recordBytes, 'C'));
}

// 显存大小按芯片选择：A5 每 block 200MB，A2/A3 每 block 100MB（DbiRecordTaskHelper::GetDbiRecordMemSize）
TEST_F(MemoryChartSegmentTest, dbi_record_mem_size_uses_chip_block_size)
{
    struct Case {
        const char *soc;
        uint64_t blockDataSize;
    };
    const Case cases[] = {
        {"Ascend950PR", MC_BLOCK_DATA_SIZE_SIMT},
        {"Ascend910B", MC_BLOCK_DATA_SIZE_SIMD},
    };
    for (const auto &item : cases) {
        DeviceContext::Local().SetSocVersion(item.soc);
        EXPECT_EQ(DbiRecordTaskHelper::GetDbiRecordMemSize(ProfDBIType::MEMORY_CHART, 1U, 1U),
            item.blockDataSize + BLOCK_GAP);
    }
}

// A2/A3 没有 SIMT，按 100MB/block 回收，整块单槽布局下记录完整上报（DataCollectInDevice::GenDBIData）
TEST_F(MemoryChartSegmentTest, a2_a3_uses_simd_block_size)
{
    constexpr uint64_t segmentArea = MC_BLOCK_DATA_SIZE_SIMD - sizeof(BlockHeader);
    constexpr uint64_t blockMemSize = MC_BLOCK_DATA_SIZE_SIMD + BLOCK_GAP;
    DeviceContext::Local().SetSocVersion("Ascend910B");
    gBlockWriter = [](uint8_t *block) {
        SetBlockLayout(block, LAYOUT_PURE_SIMD);
        WriteSegment(block, LAYOUT_PURE_SIMD, 0, 2, 2 * TEST_RECORD_BYTES, 0, 'A', true, segmentArea);
    };
    DumpOnce(blockMemSize);
    ASSERT_EQ(gSentMessages.size(), 2U);
    const auto header = GetSentHeader(gSentMessages[0]);
    EXPECT_EQ(header.count, 2U);
    EXPECT_EQ(header.length, 2 * TEST_RECORD_BYTES);
    EXPECT_EQ(GetSentPayload(gSentMessages[0]), std::string(2 * TEST_RECORD_BYTES, 'A'));
}

// ACL 入口拿不到注册上下文、rt 入口查不到注册事件时按含 SIMT 保守分槽；非 MemoryChart 模式不写布局（DbiRecordTaskHelper::InitMemoryChartLayout（ACL/rt 两个重载））
TEST_F(MemoryChartSegmentTest, layout_helper_is_conservative_when_context_unavailable)
{
    std::vector<uint8_t> memInfo(TEST_BLOCK_MEM_SIZE, 0);
    ProfDataCollect collector(nullptr, false);
    EXPECT_TRUE(DbiRecordTaskHelper::InitMemoryChartLayout(&collector, ProfDBIType::MEMORY_CHART,
        RegisterContextSP(), memInfo.data(), memInfo.size()));
    ASSERT_EQ(gWrittenLayoutIds.size(), 1U);
    EXPECT_EQ(gWrittenLayoutIds[0], LAYOUT_WITH_SIMT);

    gWrittenLayoutIds.clear();
    std::vector<uint8_t> memInfoByRegId(TEST_BLOCK_MEM_SIZE, 0);
    EXPECT_TRUE(DbiRecordTaskHelper::InitMemoryChartLayout(&collector, ProfDBIType::MEMORY_CHART,
        UINT64_MAX, memInfoByRegId.data(), memInfoByRegId.size()));
    ASSERT_EQ(gWrittenLayoutIds.size(), 1U);
    EXPECT_EQ(gWrittenLayoutIds[0], LAYOUT_WITH_SIMT);

    gWrittenLayoutIds.clear();
    EXPECT_TRUE(DbiRecordTaskHelper::InitMemoryChartLayout(&collector, ProfDBIType::OPERAND_RECORD,
        UINT64_MAX, memInfoByRegId.data(), memInfoByRegId.size()));
    EXPECT_TRUE(gWrittenLayoutIds.empty());
}
