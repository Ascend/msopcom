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
#ifndef __BASIC_DEFS_H__
#define __BASIC_DEFS_H__

#include <cstdint>
constexpr uint16_t PATH_MAX_LENGTH = 4096U;
constexpr uint16_t NAME_MAX_LENGTH = 1024U;
constexpr uint32_t UINT32_INVALID = UINT32_MAX;
constexpr uint32_t PMU_EVENT_MAX_NUM = 8U;
constexpr uint32_t EVENT_MAX_NUM = 96U;
constexpr uint32_t PMU_EVENT_MAX_NUM_A5 = 10U;
constexpr uint32_t EVENT_MAX_NUM_A5 = 60U;

enum class ReplayMode : uint8_t {
    KERNEL = 0,
    APPLICATION,
    RANGE,
};
#endif // __BASIC_DEFS_H__
