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
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "utils/InjectLogger.h"

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

    /**
     * @brief 校验 ELF 头并定位 .text 段序号、SHT_SYMTAB 段头及其关联字符串表段头。
     * @param data 原始 ELF 数据指针（原地解析，不拷贝二进制）
     * @param length 数据长度
     * @param symtab 输出的 SHT_SYMTAB 段头
     * @param strtab 输出的符号字符串表段头（symtab.sh_link 指向）
     * @param textIndex 输出的 .text 段序号
     * @param foundText 输出是否存在 .text 段
     * @return 定位是否成功
     */
    static bool LocateKernelSymtab(const char *data, std::size_t length, Elf64_Shdr &symtab, Elf64_Shdr &strtab,
        std::size_t &textIndex, bool &foundText);

    /**
     * @brief 直接解析裸 buffer 的节头表（不拷贝整块二进制），用于 GB 级内核二进制。
     * @param data 原始 ELF 数据指针
     * @param length 数据长度
     * @param sections 输出的节名 -> 节头映射
     * @param allSections 可选，额外输出所有同名节（不覆盖）
     * @return 解析是否成功
     */
    static bool ScanSectionHeaders(const char *data, std::size_t length, std::map<std::string, Elf64_Shdr> &sections,
        std::multimap<std::string, Elf64_Shdr> *allSections = nullptr);

private:
    Elf64_Ehdr header_{};
    std::vector<char> buffer_;
    std::map<std::string, Elf64_Shdr> sections_;
    std::multimap<std::string, Elf64_Shdr> allSections_; // 包含所有同名section（不覆盖）
};

namespace elf {

/**
 * @brief 从裸 buffer 的特定偏移处读取一个值（直接在 data 上解析，不拷贝整块 buffer）
 * @tparam T 需要读取的类型
 * @param data buffer 起始指针
 * @param length buffer 长度
 * @param offset 偏移位置
 * @param value 读取到的值
 */
template <typename T> bool ReadValue(const char *data, std::size_t length, std::size_t offset, T &value) {
    static_assert(std::is_standard_layout<T>::value, "T is not standard layout.");
    if (data == nullptr || offset > length || length - offset < sizeof(T)) {
        ERROR_LOG("Read out of bound, data [0, %zu), read [%zu, %zu).", length, offset, offset + sizeof(T));
        return false;
    }
    std::copy_n(data + offset, sizeof(T), reinterpret_cast<char *>(&value));
    return true;
}

/**
 * @brief 从裸 buffer 的特定偏移处读取若干个连续的 T（直接在 data 上解析，不拷贝整块 buffer）
 * @tparam T 需要读取的类型
 * @param data buffer 起始指针
 * @param length buffer 长度
 * @param offset 偏移位置
 * @param count 需要读取的元素个数
 * @param values 读取到的值数组
 */
template <typename T>
bool ReadArray(const char *data, std::size_t length, std::size_t offset, std::size_t count, std::vector<T> &values) {
    static_assert(std::is_standard_layout<T>::value, "T is not standard layout.");
    if (data == nullptr || count > SIZE_MAX / sizeof(T) || offset > length || length - offset < count * sizeof(T)) {
        ERROR_LOG("Read out of bound or overflow, data [0, %zu), read [%zu, %zu).", length, offset,
            offset + count * sizeof(T));
        return false;
    }
    values.resize(count);
    std::copy_n(data + offset, count * sizeof(T), reinterpret_cast<char *>(values.data()));
    return true;
}

/**
 * @brief 按节名拼接所有同名节的原始字节（原地解析，不拷贝整块二进制）
 * @param data 原始 ELF 数据指针
 * @param length 数据长度
 * @param allSections 节名 -> 节头（含同名节，按段表顺序）
 * @param name 节名
 * @param out 输出的字节容器（std::vector<char> / std::vector<uint8_t>），会被清空后写入目标节
 * @return 是否成功（未找到或越界返回 false）
 */
template <typename Container>
bool ReadAllRawData(const char *data, std::size_t length, const std::multimap<std::string, Elf64_Shdr> &allSections,
    const std::string &name, Container &out) {
    out.clear();
    auto range = allSections.equal_range(name);
    if (range.first == range.second) {
        ERROR_LOG("No section %s found.", name.c_str());
        return false;
    }
    for (auto it = range.first; it != range.second; ++it) {
        const Elf64_Shdr &header = it->second;
        if (header.sh_size > length || header.sh_offset > length - header.sh_size) {
            ERROR_LOG("Section %s has invalid data: sh_size(%zu), available buffer(%zu)", name.c_str(), header.sh_size,
                length - header.sh_offset);
            out.clear();
            return false;
        }
        out.insert(out.end(), data + header.sh_offset, data + header.sh_offset + header.sh_size);
    }
    return true;
}

} // namespace elf
