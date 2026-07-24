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

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>


template<typename StructT, std::size_t alignSize, typename = void>
struct StructAlignTo : public StructT {
    static_assert(alignSize > sizeof(StructT), "alignSize must greater than or equal to size of struct");
    uint8_t reserved[alignSize - sizeof(StructT)];
};

template<typename StructT, std::size_t alignSize>
struct StructAlignTo<StructT, alignSize, typename std::enable_if<(sizeof(StructT) == alignSize)>::type> : public StructT {};

template<typename StructT, std::size_t alignSize, typename = void>
struct StructAlignBy : public StructT {
    uint8_t reserved[alignSize - sizeof(StructT) % alignSize];
};

template<typename StructT, std::size_t alignSize>
struct StructAlignBy<StructT, alignSize, typename std::enable_if<(sizeof(StructT) % alignSize == 0)>::type> : public StructT {};
