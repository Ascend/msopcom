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

#ifndef __DBI_DEFS_H__
#define __DBI_DEFS_H__

#include <cstddef>
#include <cstdint>
#include "BasicDefs.h"

constexpr uint16_t INSTR_PROF_MEMSIZE = 8;
constexpr uint64_t BLOCK_GAP = 64U; // 每个block间需要间隔64B
constexpr uint64_t MAX_BLOCK = 108U; // 最大108个block
constexpr uint64_t SIMT_THREAD_GAP = 64U; // 每个SIMT thread间需要间隔64B
constexpr uint32_t MAX_THREAD_NUM = 2048U; // SIMT thread个数
// 每个 block 的数据区大小按芯片区分：带 SIMT 线程的芯片（A5）需要给每个线程预留独立分区，取 200MB；
// 没有 SIMT 的芯片（A2/A3）只有一个 SIMD 分区，100MB 即可，避免占用过多显存。
constexpr uint64_t MC_BLOCK_DATA_SIZE_SIMT = 200U * 1024 * 1024;
constexpr uint64_t MC_BLOCK_DATA_SIZE_SIMD = 100U * 1024 * 1024;
// 协议与主机缓冲的上界，取两者最大值
constexpr uint64_t MC_MAX_BLOCK_DATA_SIZE = MC_BLOCK_DATA_SIZE_SIMT;
// 设备桩按架构选择块大小；主机侧不能直接用这个常量，需按芯片运行期选择（见 GetMemoryChartBlockDataSize）
#if defined(__NPU_ARCH__) && (__NPU_ARCH__ == 3101 || __NPU_ARCH__ == 3510)
constexpr uint64_t MAX_BLOCK_DATA_SIZE = MC_BLOCK_DATA_SIZE_SIMT;
#elif defined(__CCE_IS_AICORE__) && __CCE_IS_AICORE__ == 1
constexpr uint64_t MAX_BLOCK_DATA_SIZE = MC_BLOCK_DATA_SIZE_SIMD;
#else
constexpr uint64_t MAX_BLOCK_DATA_SIZE = MC_MAX_BLOCK_DATA_SIZE;
#endif
constexpr uint64_t BLOCK_MEM_SIZE = MAX_BLOCK_DATA_SIZE + BLOCK_GAP; // 每个block占用内存大小

// 主机侧按芯片选择 memory chart 每个 block 的数据区大小，必须与设备桩的编译期选择保持一致
inline uint64_t GetMemoryChartBlockDataSize(bool isAscend950)
{
    return isAscend950 ? MC_BLOCK_DATA_SIZE_SIMT : MC_BLOCK_DATA_SIZE_SIMD;
}
constexpr char const *OPERAND_RECORD = "OperandRecord.bin";
constexpr char const *WARP_TIMELINE = "WarpTimeline.bin";
constexpr char const *DFX_TUNE_LOG = "dfx_tune.log";
constexpr char const *DFX_REGION_MAP = "dfx_region_map.txt";
constexpr uint32_t WARP_NUM_PER_BLOCK = 64U; // 每个block固定64个warp

enum class ProfDBIType {
    AS_IS = 0, // 不插桩
    OPERAND_RECORD, // operand record桩
    MEMORY_CHART, // memory chart桩
    INSTR_PROF_START, // start桩
    INSTR_PROF_END, // end桩
    INSTR_PROF_DFX, // dfx桩
    BB_COUNT, // bb count桩
    WARP_TIMELINE // warp timeline桩
};

constexpr uint32_t DBI_FLAG_OPERAND_RECORD = 1U << static_cast<uint32_t>(ProfDBIType::OPERAND_RECORD);
constexpr uint32_t DBI_FLAG_MEMORY_CHART = 1U << static_cast<uint32_t>(ProfDBIType::MEMORY_CHART);
constexpr uint32_t DBI_FLAG_INSTR_PROF_START = 1U << static_cast<uint32_t>(ProfDBIType::INSTR_PROF_START);
constexpr uint32_t DBI_FLAG_INSTR_PROF_END = 1U << static_cast<uint32_t>(ProfDBIType::INSTR_PROF_END);
constexpr uint32_t DBI_FLAG_INSTR_PROF_DFX = 1U << static_cast<uint32_t>(ProfDBIType::INSTR_PROF_DFX);
constexpr uint32_t DBI_FLAG_BB_COUNT = 1U << static_cast<uint32_t>(ProfDBIType::BB_COUNT);
constexpr uint32_t DBI_FLAG_WARP_TIMELINE = 1U << static_cast<uint32_t>(ProfDBIType::WARP_TIMELINE);

enum class OperandType : uint8_t {
    // 浮点数据类型 - 新增浮点类型必须添加在 DATA_FLOAT_MAX 之前
    DATA_F16 = 0,
    DATA_F16X2,
    DATA_BF16,
    DATA_BF16X2,
    DATA_E4M3,
    DATA_E5M2,
    DATA_E1M2,
    DATA_E2M1,
    DATA_V2BF16,
    DATA_V2F16,
    DATA_HIF8X2,
    DATA_F8E4M3X2,
    DATA_F8E5M2X2,
    DATA_FMIX,
    DATA_HALF,
    DATA_F32,
    DATA_F32X2,
    DATA_FLOAT_MAX = DATA_F32X2, // 浮点类型结束标记，添加新类型后需更新

    // 整数数据类型
    DATA_B4,
    DATA_B8,
    DATA_B16,
    DATA_B32,
    DATA_B64,
    DATA_B128,
    DATA_S16,
    DATA_S32,
    DATA_U16,
    DATA_U32,
    DATA_S8,
    DATA_U8,
    DATA_U64,
    DATA_S64,
    DATA_SX32,
    DATA_ZX32,

    // 结束标记（非数据类型）
    END
};

struct OperandHeader {
    uint32_t magicWords;
    uint32_t reverse;
};

#pragma pack(4)
struct WarpRecord {
    uint64_t startTime;
    uint64_t endTime;
};

struct WarpHeader {
    uint32_t magicWords;
    uint32_t warpCount;
    uint32_t coreId;
    uint32_t coreType;
};

struct OperandRecord {
    uint64_t instructions{};
    uint64_t operands{};
    uint64_t funcType{};
};

// MemoryChart 的记录计数与长度已下沉到 MemoryChartSegmentHeader，此处仅保留各指令的参数暂存字段
struct BlockHeader {
    uint64_t threadNum; // MemoryChart 布局标识，不是实际线程数：0 表示纯 SIMD（整块单槽），
                        // MAX_THREAD_NUM 表示含 SIMT 或识别不确定（MAX_THREAD_NUM + 1 槽）
    uint64_t length;    // host 预写的布局魔数/版本（MC_LAYOUT_TAG），记录长度见 MemoryChartSegmentHeader
    uint64_t ndPara;   // #3023 set_nd_para，MOV_FP类指令使用
    uint64_t loop3Para;   // #set_loop3Para_para，FIX_L0C_TO_OUT类指令使用
    uint64_t channelPara;   // #set_channel_para，FIX_L0C_TO_OUT类指令使用
    uint64_t loopSizeOuttol1;      // set_loop_size_outtol1，MOV_OUT_TO_L1_ALIGN_V2指令使用
    uint64_t loop1StrideOuttol1;   // set_loop1_stride_outtol1，MOV_OUT_TO_L1_ALIGN_V2类指令使用
    uint64_t loop2StrideOuttol1;   // set_loop2_stride_outtol1，MOV_OUT_TO_L1_ALIGN_V2类指令使用
    uint64_t loopSizeOuttoub;      // set_loop_size_outtoub，MOV_OUT_TO_L1_ALIGN_V2指令使用
    uint64_t loop1StrideOuttoub;   // set_loop1_stride_outtoub，MOV_OUT_TO_L1_ALIGN_V2指令使用
    uint64_t loop2StrideOuttoub;   // set_loop2_stride_outtoub，MOV_OUT_TO_L1_ALIGN_V2指令使用
    uint64_t loopSizeUbToOut; // #3709 set_loop_size_para_ubtout，MOV_UB_TO_OUT_ALIGN_V2指令使用
    uint64_t loop1StrideUbToOut; // #2991 set_loop1_size_para_ubtout，MOV_UB_TO_OUT_ALIGN_V2指令使用
    uint64_t loop2StrideUbToOut; // #2996 set_loop2_size_para_ubtout，MOV_UB_TO_OUT_ALIGN_V2指令使用
    uint64_t mte2NzPara;           // set_mte2_nz_para，MOV_OUT_TO_L1_MULTI_ND2NZ指令使用
    uint64_t padCntNdDma;          // set_pad_cnt_nddma，NDDMA_OUT_TO_UB指令使用
    uint64_t loop0StrideNdDma;     // set_loop0_stride_nddma，NDDMA_OUT_TO_UB指令使用
    uint64_t loop1StrideNdDma;     // set_loop1_stride_nddma，NDDMA_OUT_TO_UB指令使用
    uint64_t loop2StrideNdDma;     // set_loop2_stride_nddma，NDDMA_OUT_TO_UB指令使用
    uint64_t loop3StrideNdDma;     // set_loop3_stride_nddma，NDDMA_OUT_TO_UB指令使用
    uint64_t loop4StrideNdDma;     // set_loop4_stride_nddma，NDDMA_OUT_TO_UB指令使用
    uint64_t mte2SrcPara;          // set_mte2_src_para，LOAD_OUT_TO_L1_2Dv2指令使用
};

// MemoryChart 按 (block, slot) 划分独占分区存储，槽位规则与 OperandRecord 保持一致：
// 0 号槽固定留给 SIMD（非 SIMT）记录，SIMT 线程 threadId 使用第 threadId + 1 个槽。
// 每个槽只有一个写者，槽内的计数不需要原子操作，从而避免并发读改写造成的记录覆盖。
// 布局只有两档，由 host 在 launch 前写入 BlockHeader.threadNum 作为布局标识，整个 launch 内只读：
//   - 0：确认二进制不含 SIMT，0 号槽独占整块，纯 SIMD 算子可用满全部容量；
//   - MAX_THREAD_NUM：含 SIMT 或识别结果不确定，固定 MAX_THREAD_NUM + 1 个槽，保守预留所有线程槽位。
// 不依赖运行期实际线程数，因此普通 launch 内含 SIMT 区域时也不会漏记或越界。
struct MemoryChartSegmentHeader {
    uint64_t count{0};   // 本分区已写入的记录条数
    uint64_t length{0};  // 本分区已写入的字节数，不包含本结构体
    uint64_t dropped{0}; // 本分区容量不足而丢弃的记录条数
};

constexpr uint64_t MC_SEGMENT_HEADER_SIZE = sizeof(MemoryChartSegmentHeader);
// 设备侧单个 block 内除 BlockHeader 外可用于存放分区的全部空间（按芯片编译期确定）。
// 仅设备侧编译（以及设备桩头文件）使用；host 侧必须按 GetMemoryChartBlockDataSize 运行期计算，
// 否则在 A2/A3 上会误用 200MB 口径。
constexpr uint64_t MC_SEGMENT_AREA = MAX_BLOCK_DATA_SIZE - sizeof(BlockHeader);

// MemoryChart 记录按 #pragma pack(4) 紧凑排列，长度随指令类型不同（RecordHeader + 各 Record 结构体，
// 实测 52 ~ 112 字节，不是 8 的整数倍），因此 host 侧不能用固定长度或对齐做整除校验，只用 8 字节作 count 的宽松下界。
constexpr uint64_t MC_MIN_RECORD_BYTES = 8U;
// 布局魔数 + 版本号，host 在 launch 前写入 BlockHeader.length，回收数据时校验。
// 用于识别 host 与设备桩布局口径不一致（混版、A5/A2/A3 block 大小取错等），避免把旧格式静默当成分区解析。
// ASCII "MCLAYOU" + 版本 1
constexpr uint64_t MC_LAYOUT_TAG = 0x4D434C41594F5501ULL;

// host 在 launch 前写入 block 头前 16 字节的布局信息，与 BlockHeader 的前两个字段一一对应
struct MemoryChartLayoutHeader {
    uint64_t layoutId;
    uint64_t layoutTag;
};

// 该布局同时被 host、设备桩和不同架构的编译器使用，尺寸/偏移一旦变化会导致跨版本静默错读。
// 断言失败时必须同步提升 MC_LAYOUT_TAG 版本号，避免旧数据被按新布局解析。
static_assert(sizeof(BlockHeader) == 176U, "BlockHeader size changed, bump MC_LAYOUT_TAG");
static_assert(offsetof(BlockHeader, threadNum) == 0U, "layout id must stay at offset 0");
static_assert(offsetof(BlockHeader, length) == 8U, "layout tag must stay at offset 8");
static_assert(sizeof(MemoryChartSegmentHeader) == 24U, "MemoryChartSegmentHeader size changed");
static_assert(sizeof(MemoryChartLayoutHeader) == 16U, "layout header must match BlockHeader head");
// 槽位偏移按 8 字节对齐，BlockHeader 必须是 8 的整数倍
static_assert(sizeof(BlockHeader) % 8U == 0U, "segment offset must stay 8-byte aligned");

// 设备侧编译时，这些布局计算需要能在 __aicore__ / __simt_callee__ 函数中调用；主机侧按普通 inline 处理
#if defined(__CCE_IS_AICORE__) && __CCE_IS_AICORE__ == 1
#ifdef SIMT_MODE
#define MC_LAYOUT_INLINE __simt_callee__ __aicore__ inline
#else
#define MC_LAYOUT_INLINE __aicore__ inline
#endif
#else
#define MC_LAYOUT_INLINE inline
#endif

// 布局标识是否合法：只有两档，其他值说明 host 预写或设备读取异常
MC_LAYOUT_INLINE bool MemoryChartLayoutIsValid(uint64_t layoutId)
{
    return layoutId == 0U || layoutId == MAX_THREAD_NUM;
}

// 槽位数：纯 SIMD（layoutId == 0）只有 0 号槽并独占整块；否则固定 0 号槽 + 每个 SIMT 线程一个槽
MC_LAYOUT_INLINE uint64_t MemoryChartSegmentNum(uint64_t layoutId)
{
    return (layoutId == 0U) ? 1U : MAX_THREAD_NUM + 1U;
}

// 槽步长：按槽位数均分整块，纯 SIMD 场景独占整块
MC_LAYOUT_INLINE uint64_t MemoryChartSegmentStride(uint64_t segmentArea, uint64_t layoutId)
{
    return (segmentArea / MemoryChartSegmentNum(layoutId)) & ~0x7ULL; // 8 字节对齐
}

MC_LAYOUT_INLINE uint64_t MemoryChartSegmentOffsetInBlock(uint64_t segmentArea, uint64_t segIdx, uint64_t layoutId)
{
    return sizeof(BlockHeader) + segIdx * MemoryChartSegmentStride(segmentArea, layoutId);
}

MC_LAYOUT_INLINE uint64_t MemoryChartSegmentCapacity(uint64_t segmentArea, uint64_t layoutId)
{
    return MemoryChartSegmentStride(segmentArea, layoutId) - MC_SEGMENT_HEADER_SIZE;
}

struct DBIDataHeader {
    uint64_t count;      // 该block记录的条目数
    uint64_t length;     // Header后紧跟的数据长度，也就是输出路径长度或者动态插桩数据长度
    uint64_t overflow;   // 缓冲区不足而未记录的数据条目数
    uint16_t blockId;
    uint8_t endFlag;    // 该path下所有block的数据都发送完成
};
#pragma pack()
#endif // __DBI_DEFS_H__
