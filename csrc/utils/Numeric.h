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


#ifndef __UTILS_NUMERIC_H__
#define __UTILS_NUMERIC_H__

template <uint32_t alignSize, typename T> inline T CeilByAlignSize(T v) {
    static_assert(alignSize != 0, "align size cannot be zero");
    return ((v + static_cast<T>(alignSize) - 1) / static_cast<T>(alignSize)) * static_cast<T>(alignSize);
}

template <uint32_t alignSize, typename T> inline T AlignDownSize(T val) {
    static_assert(alignSize != 0, "align size cannot be zero");
    return val - static_cast<T>((val % static_cast<T>(alignSize)));
}

#endif // __UTILS_NUMERIC_H__
