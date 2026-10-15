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

#include <elf.h>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

using Buffer = std::vector<char>;

inline Buffer operator+(Buffer lhs, Buffer rhs)
{
    lhs.reserve(lhs.size() + rhs.size());
    std::copy(rhs.cbegin(), rhs.cend(), std::back_inserter(lhs));
    return lhs;
}

Elf64_Ehdr CreateValidElfHeader(void);

Buffer CreateValidNameSection(void);

Buffer CreateValidElf(void);

/**
 * @brief 构造一个包含 .text/.symtab/.strtab/.shstrtab 的最小合法 ELF，用于内核符号解析用例。
 * @param kernelNames   .text 中的 global FUNC 符号名（写入 .symtab，st_value 依次为 0x10*(i+1)）
 * @param simtNames     .text 中的 local 符号名（用于触发 SIMT 检测，通常为 "$t*"）
 * @param strtabAtEnd   true 时把 .strtab 放到文件末尾（其末尾字节紧贴 buffer 末尾）
 * @param lastSimtNoNul true 时最后一个 SIMT 名字不写结尾 '\0'（用于越界边界用例）
 */
std::vector<uint8_t> CreateElfWithKernelSymbols(const std::vector<std::string> &kernelNames,
    const std::vector<std::string> &simtNames = {}, bool strtabAtEnd = false, bool lastSimtNoNul = false);
