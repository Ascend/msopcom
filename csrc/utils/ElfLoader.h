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
#include <map>
#include <string>
#include <vector>

/**
 * @brief Elf 类表示一个 Elf 文件内容，我们可以从中获取某个 section 的原始数据，
 * 或者如果某个 section 是 StringTable，我们可以直接读取这个 StringTable。
 * 为了方便进行错误处理，我们不直接使用 Elf 类进行 ELF 文件的读取，而采取 builder 模式，
 * 由下面的 ElfLoader 类进行实际的读取和校验。
 */
class Elf {
public:
    friend class ElfLoader;

    /**
     * @brief 从 Elf 对象内读取某个 section 的原始数据
     * @param name section 名称
     * @return 该 section 的原始数据
     */
    std::vector<char> ReadRawData(const std::string &name) const;

    /**
     * @brief 从 Elf 对象内读取某个 section 的 StringTable
     * @desc StringTable 是一种在 elf 内常用的字符串保存格式
     * 其中的数据是若干以 '\0' 结尾的字符串拼接(CStr)
     * 其标准保证其内容的第一个及最后一个字节都是 '\0'
     * @param name section 名称
     * @return 该 section 的 StringTable
     */
    std::vector<std::string> ReadStringTable(const std::string &name) const;

    /**
     * @brief 从 Elf 对象内读取某个 section 的全部原始数据（包括同名 section）
     * @desc 当 ELF 中存在多个同名的 section 时（如多个 .ascend.meta.*），
     * 将所有同名 section 的数据拼接后返回。
     * @param name section 名称
     * @return 所有同名 section 拼接后的原始数据
     */
    std::vector<char> ReadAllRawData(const std::string &name) const;

    auto GetSectionHeaders(void) const -> std::map<std::string, Elf64_Shdr> const &
    {
        return this->sections_;
    }

private:
    Elf(std::map<std::string, Elf64_Shdr> sections, std::multimap<std::string, Elf64_Shdr> allSections,
        std::vector<char> buffer)
        : buffer_(std::move(buffer)), sections_(std::move(sections)), allSections_(std::move(allSections)) {}

    std::vector<char> buffer_;
    std::map<std::string, Elf64_Shdr> sections_;
    std::multimap<std::string, Elf64_Shdr> allSections_; // 包含所有同名section（不覆盖）
};

/**
 * @brief 这个类负责加载 ELF 文件，如果加载成功则通过 Load 接口返回已加载的 Elf 对象。
 */
class ElfLoader {
public:
    /**
     * @brief 试图从 buffer 构造 Elf 对象
     * @param buffer
     * @return 构造是否成功，成功之后可以使用 .Load 获取构造的 Elf 对象
     */
    bool FromBuffer(const std::vector<char> &buffer);

    Elf Load() const { return Elf{sections_, allSections_, buffer_}; }

    static bool LoadHeader(const std::vector<char> &buffer, Elf64_Ehdr &header);

private:
    Elf64_Ehdr header_{};
    std::vector<char> buffer_;
    std::map<std::string, Elf64_Shdr> sections_;
    std::multimap<std::string, Elf64_Shdr> allSections_; // 包含所有同名section（不覆盖）
};
