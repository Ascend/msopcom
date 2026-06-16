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


#include <algorithm>
#include "utils/InjectLogger.h"

#include "ElfLoader.h"

namespace elf {

/**
 * @brief 对读入的 ELF 文件头做基本校验，保证正常读取映射信息
 * @param elfHeader ELF 文件头
 * @return 校验是否通过
 */
inline bool ValidateElfHeader(const Elf64_Ehdr &elfHeader)
{
    if (elfHeader.e_ident[EI_MAG0] != ELFMAG0 || elfHeader.e_ident[EI_MAG1] != ELFMAG1 ||
        elfHeader.e_ident[EI_MAG2] != ELFMAG2 || elfHeader.e_ident[EI_MAG3] != ELFMAG3) {
        // magic不对，非elf格式文件
        ERROR_LOG("Invalid ELF magic, this is not a elf file.");
        return false;
    }
    if (elfHeader.e_ident[EI_CLASS] != ELFCLASS64) {
        // 非64位elf文件，不支持
        ERROR_LOG("32bit executable is not supported.");
        return false;
    }
    if (elfHeader.e_ident[EI_DATA] != ELFDATA2LSB) {
        // 非小端序elf文件，不支持
        ERROR_LOG("Big endian executable is not supported.");
        return false;
    }
    if (elfHeader.e_shnum <= elfHeader.e_shstrndx) {
        // 特殊section序号(.shstrtab，内部保存各个section的名字)超过section个数
        ERROR_LOG("Invalid Elf file.");
        return false;
    }
    return true;
}

/**
 * @brief 对读入的 ELF 文件头做安全长度校验
 * @param elfHeader ELF 文件头
 * @param maxSafeSize 最大安全长度
 * @return 校验是否通过
 */
inline bool ValidateElfHeaderSafety(const Elf64_Ehdr &elfHeader, std::size_t maxSafeSize)
{
    if (maxSafeSize < elfHeader.e_ehsize || maxSafeSize < elfHeader.e_phentsize
        || maxSafeSize < elfHeader.e_phnum || maxSafeSize < elfHeader.e_shentsize
        || maxSafeSize < elfHeader.e_shnum || maxSafeSize < elfHeader.e_shstrndx) {
        return false;
    }

    if (maxSafeSize < static_cast<size_t>(elfHeader.e_shoff) +
        static_cast<size_t>(elfHeader.e_shnum - 1) * static_cast<size_t>(elfHeader.e_shentsize)) {
        return false;
    }

    return true;
}

/**
 * @brief 从 buffer 内的特定偏移处读取一个值
 * @tparam T 需要读取的类型
 * @param buffer 要解析的 buffer
 * @param offset 偏移位置
 * @param value 读取到的值
 */
template <typename T>
bool ReadValueFromBuffer(const std::vector<char> &buffer, std::size_t offset, T &value)
{
    static_assert(std::is_standard_layout<T>::value, "T is not standard layout.");
    if (buffer.size() < offset + sizeof(T)) {
        ERROR_LOG("Read out of bound, data [0, %zu), read [%zu, %zu).", buffer.size(), offset, offset + sizeof(T));
        return false;
    }
    char const *base = buffer.data() + offset;
    std::copy(base, base + sizeof(T), static_cast<char *>(static_cast<void *>(&value)));
    return true;
}

/**
 * @brief 从 buffer 内的特定偏移处读取若干个连续的 T
 * @tparam T 需要读取的类型
 * @param buffer 要解析的 buffer
 * @param offset 偏移位置
 * @param count 需要读取的元素个数
 * @param values 读取到的值数组
 */
template <typename T>
bool ReadArrayFromBuffer(const std::vector<char> &buffer, std::size_t offset, std::size_t count, std::vector<T> &values)
{
    static_assert(std::is_standard_layout<T>::value, "T is not standard layout.");
    if (count > SIZE_MAX / sizeof(T) || buffer.size() < offset + count * sizeof(T)) {
        ERROR_LOG("Read out of bound or overflow, data [0, %zu), read [%zu, %zu).", buffer.size(), offset,
            offset + count * sizeof(T));
        return false;
    }
    values.resize(count);
    char const *base = buffer.data() + offset;
    std::copy(base, base + count * sizeof(T), static_cast<char *>(values.data()));
    return true;
}

} // namespace elf

std::vector<char> Elf::ReadRawData(const std::string &name) const
{
    auto iter = sections_.find(name);
    if (iter == sections_.end()) {
        ERROR_LOG("No section %s found.", name.c_str());
        return {};
    }
    std::vector<char> values;
    elf::ReadArrayFromBuffer<char>(buffer_, iter->second.sh_offset, iter->second.sh_size, values);
    return values;
}

std::vector<std::string> Elf::ReadStringTable(const std::string &name) const
{
    auto iter = sections_.find(name);
    if (iter == sections_.end()) {
        ERROR_LOG("No section %s found.", name.c_str());
        return {};
    }
    auto &header = iter->second;
    // 目前编译器那边还不知道怎么设置section类型，因此这里添加一个额外的判定
    if (header.sh_offset < buffer_.size() && buffer_[header.sh_offset] != 0) {
        ERROR_LOG("Section %s is not a valid StringTable.", name.c_str());
        return {};
    }
    if (!(header.sh_size > 2 && buffer_.size() - header.sh_offset > 2)) {
        ERROR_LOG("Section %s has invalid data for a StringTable: sh_size(%zu), available buffer(%zu)",
            name.c_str(), header.sh_size, buffer_.size() - header.sh_offset);
        return {};
    }

    std::vector<std::string> result;
    std::string section_data = std::string(buffer_.data() + header.sh_offset + 1,
        std::min(header.sh_size - 2, buffer_.size() - header.sh_offset - 2));
    SplitString(section_data, '\0', result);
    return result;
}

bool ElfLoader::LoadHeader(const std::vector<char> &buffer, Elf64_Ehdr &header)
{
    return elf::ReadValueFromBuffer<Elf64_Ehdr>(buffer, 0, header);
}

bool ElfLoader::FromBuffer(const std::vector<char> &buffer)
{
    if (buffer.empty()) {
        ERROR_LOG("ELF contains no data.");
        return false;
    }
    buffer_ = buffer;
    // 读取文件头并进行简单校验
    if (!elf::ReadValueFromBuffer<Elf64_Ehdr>(buffer, 0, header_)) {
        ERROR_LOG("Read elf header failed when load elf from buffer");
        return false;
    }
    if (!elf::ValidateElfHeader(header_)) {
        ERROR_LOG("validate elf header failed when load elf from buffer");
        return false;
    }
    if (!elf::ValidateElfHeaderSafety(header_, buffer_.size())) {
        return false;
    }
    // 各个 section 的名字保存在一个类型 StringTable 的 section 内(一般为 .shstrtab)
    // ELF 头部的 e_shstrndx 字段为这个 section 的序号，通过序号我们可以找到这个 section 头部
    Elf64_Shdr nameSecHeader {};
    if (!elf::ReadValueFromBuffer<Elf64_Shdr>(buffer,
        header_.e_shoff + static_cast<Elf64_Off>(header_.e_shstrndx) * header_.e_shentsize, nameSecHeader)) {
        ERROR_LOG("read name section header failed when load elf from buffer");
        return false;
    }

    std::vector<char> nameBuffer;
    if (!elf::ReadArrayFromBuffer<char>(buffer, nameSecHeader.sh_offset, nameSecHeader.sh_size, nameBuffer)) {
        ERROR_LOG("read header names failed when load elf from buffer");
        return false;
    }
    for (unsigned long idx = 0; idx < header_.e_shnum; idx++) {
        // 通过 elf 头部信息，读取各个 section 的头部
        // 各个 section 头部内有一个 sh_name 字段指向其名字在 StringTable 里的起点
        Elf64_Shdr sectionHeader {};
        if (!elf::ReadValueFromBuffer<Elf64_Shdr>(buffer,
            static_cast<size_t>(header_.e_shoff) + idx * static_cast<size_t>(header_.e_shentsize),
            sectionHeader)) {
            ERROR_LOG("read elf section header failed when load elf from buffer");
            return false;
        }

        if (sectionHeader.sh_name >= nameBuffer.size() || nameBuffer.data()[nameBuffer.size() - 1] != '\0') {
            ERROR_LOG(
                "section name offset %u out of bounds or nameBuffer not null-terminated.", sectionHeader.sh_name);
            return false;
        }
        std::string sectionName = std::string{nameBuffer.data() + sectionHeader.sh_name};
        sections_[sectionName] = sectionHeader;
    }
    return true;
}
