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

#include <cstdint>
#include "BasicDefs.h"

constexpr uint16_t INSTR_PROF_MEMSIZE = 8;
constexpr uint64_t BLOCK_GAP = 64U; // 每个block间需要间隔64B
constexpr uint64_t MAX_BLOCK = 108U; // 最大108个block
constexpr uint64_t SIMT_THREAD_GAP = 64U; // 每个SIMT thread间需要间隔64B
constexpr uint32_t MAX_THREAD_NUM = 2048U; // SIMT thread个数
constexpr uint64_t MAX_BLOCK_DATA_SIZE = 100U * 1024 * 1024; // 每个block存储100MB数据
constexpr uint64_t BLOCK_MEM_SIZE = MAX_BLOCK_DATA_SIZE + BLOCK_GAP; // 每个block占用内存大小
constexpr uint64_t RECORD_OVERFLOW_BIT = 1ULL << 63; // BlockHeader溢出标记位
constexpr char const *OPERAND_RECORD = "OperandRecord.bin";
enum class ProfDBIType {
    AS_IS = 0, // 不插桩
    OPERAND_RECORD, // operand record桩
    MEMORY_CHART, // memory chart桩
    INSTR_PROF_START, // start桩
    INSTR_PROF_END, // end桩
    INSTR_PROF_DFX, // dfx桩
    BB_COUNT // bb count桩
};

constexpr uint32_t DBI_FLAG_OPERAND_RECORD = 1U << static_cast<uint32_t>(ProfDBIType::OPERAND_RECORD);
constexpr uint32_t DBI_FLAG_MEMORY_CHART = 1U << static_cast<uint32_t>(ProfDBIType::MEMORY_CHART);
constexpr uint32_t DBI_FLAG_INSTR_PROF_START = 1U << static_cast<uint32_t>(ProfDBIType::INSTR_PROF_START);
constexpr uint32_t DBI_FLAG_INSTR_PROF_END = 1U << static_cast<uint32_t>(ProfDBIType::INSTR_PROF_END);
constexpr uint32_t DBI_FLAG_INSTR_PROF_DFX = 1U << static_cast<uint32_t>(ProfDBIType::INSTR_PROF_DFX);
constexpr uint32_t DBI_FLAG_BB_COUNT = 1U << static_cast<uint32_t>(ProfDBIType::BB_COUNT);

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
struct OperandRecord {
    uint64_t instructions{};
    uint64_t operands{};
    uint64_t funcType{};
};

// 考虑记录条目数过多时，把length最高位置1，剩余63位记录经过桩的请求总数，count维持不变，保留可读数据
struct BlockHeader {
    uint64_t count;    // 该block记录的条目数
    uint64_t length;   // 该block已经存储的长度，不包含BlockHeader自身，device侧使用
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

struct DBIDataHeader {
    uint64_t count;      // 该block记录的条目数
    uint64_t length;     // Header后紧跟的数据长度，也就是输出路径长度或者动态插桩数据长度
    uint64_t overflow;   // 缓冲区不足而未记录的数据条目数
    uint16_t blockId;
    uint8_t endFlag;    // 该path下所有block的数据都发送完成
};
#pragma pack()
#endif // __DBI_DEFS_H__
