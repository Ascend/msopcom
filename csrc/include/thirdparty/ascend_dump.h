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

// 这个文件本身需要与被劫持的对象一样

#ifndef __ASCEND_DUMP_H__
#define __ASCEND_DUMP_H__
#include <cstdint>
#ifdef __cplusplus
extern "C" {
#endif
enum class DfxTensorType : uint16_t {
    INVALID_TENSOR = 0,
    GENERAL_TENSOR = 1,
    INPUT_TENSOR,
    OUTPUT_TENSOR,
    WORKSPACE_TENSOR,
    ASCENDC_LOG = 5,
    MC2_CTX,
    TILING_DATA,
    L1,
    L2,
    OVERFLOW_MES = 10,
    FFTS_ADDRESS,
    SHAPE_TENSOR
};

enum class DfxPointerType : uint16_t {
    INVALID_POINTER = 0,
    LEVEL_1_POINTER = 1,
    LEVEL_2_POINTER,
    LEVEL_2_POINTER_WITH_SHAPE,
    SHAPE_TENSOR_PLACEHOLD
};

enum class DfxType : uint16_t {
    DFX_TYPE = 4,
    TIK_TYPE = 6
};
void *AdumpGetDFXInfoAddrForDynamic(uint32_t space, uint64_t &atomicIndex);
void *AdumpGetDFXInfoAddrForStatic(uint32_t space, uint64_t &atomicIndex);

#ifdef __cplusplus
} // extern "C"
#endif
#endif // __ASCEND_DUMP_H__
