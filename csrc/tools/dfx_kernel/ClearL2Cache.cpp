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

constexpr uint16_t TILE_LENGTH = 512;

extern "C" __global__[aicore] void ClearL2Cache(__gm__ void *buffer, __gm__ void *tilingSize) {
#ifdef __DAV_C220_VEC__
    return;
#endif

#if defined(__DAV_C220_CUBE__) || (defined(__CCE_AICORE__) && __CCE_AICORE__ == 200)
    uint64_t blockLen = *(__gm__ uint64_t *)tilingSize;
    __gm__ int8_t *bufferStart = (__gm__ int8_t *)buffer + blockLen * get_block_idx();
    __cbuf__ int8_t *l1Addr = 0;
    int32_t loopCount = blockLen / TILE_LENGTH;
    for (int i = 0; i < loopCount; i++) {
        copy_gm_to_cbuf(l1Addr, bufferStart + i * TILE_LENGTH, 0, 1, TILE_LENGTH / 32, 0, 0, PAD_NONE);
        pipe_barrier(PIPE_MTE2);
    }
#endif

#if defined(__DAV_C310_CUBE__)
    uint64_t blockLen = *(__gm__ uint64_t *)tilingSize;
    __gm__ int8_t *bufferStart = (__gm__ int8_t *)buffer + blockLen * get_block_idx();
    __cbuf__ int8_t *l1Addr = 0;
    int32_t loopCount = blockLen / TILE_LENGTH;
    for (int i = 0; i < loopCount; i++) {
        copy_gm_to_cbuf_align_v2(l1Addr, bufferStart + i * TILE_LENGTH, 0, 1, TILE_LENGTH, 0, 0, 0, 2, 0, 0);
        pipe_barrier(PIPE_MTE2);
        copy_gm_to_cbuf_align_v2(l1Addr, bufferStart + i * TILE_LENGTH, 0, 1, TILE_LENGTH, 0, 0, 0, 0, 0, 0);
        pipe_barrier(PIPE_MTE2);
    }
#endif
}
