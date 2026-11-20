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
#include <cstring>
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
 * @brief 从 buffer 内的特定偏移处读取一个值（vector 封装）
 */
template <typename T> bool ReadValueFromBuffer(const std::vector<char> &buffer, std::size_t offset, T &value) {
    return ReadValue<T>(buffer.data(), buffer.size(), offset, value);
}

/**
 * @brief 从 buffer 内的特定偏移处读取若干个连续的 T（vector 封装）
 */
template <typename T>
bool ReadArrayFromBuffer(
    const std::vector<char> &buffer, std::size_t offset, std::size_t count, std::vector<T> &values) {
    return ReadArray<T>(buffer.data(), buffer.size(), offset, count, values);
}

/**
 * @brief 一次性读取全部节头表项（原地解析，不拷贝整块二进制）
 * @param data 原始 ELF 数据指针
 * @param length 数据长度
 * @param header 已校验的 ELF 头
 * @param headers 输出的节头数组（下标即节序号）
 */
bool ReadSectionHeaders(
    const char *data, std::size_t length, const Elf64_Ehdr &header, std::vector<Elf64_Shdr> &headers) {
    headers.clear();
    if (header.e_shentsize != sizeof(Elf64_Shdr) || header.e_shoff == 0 || header.e_shnum == 0) {
        return false;
    }
    headers.resize(header.e_shnum);
    for (std::size_t i = 0; i < header.e_shnum; ++i) {
        if (!ReadValue<Elf64_Shdr>(data, length,
                static_cast<std::size_t>(header.e_shoff) + i * static_cast<std::size_t>(header.e_shentsize),
                headers[i])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 从已校验的 ELF 头构建「节名 -> 节头」及同名节映射，直接在 data 上原地解析、不拷贝整块二进制
 * @param data 原始 ELF 数据指针
 * @param length 数据长度
 * @param header 已校验的 ELF 头
 * @param sections 输出的节名 -> 节头映射
 * @param allSections 可选，额外输出所有同名节（不覆盖）
 */
bool BuildSectionMap(const char *data, std::size_t length, const Elf64_Ehdr &header,
    std::map<std::string, Elf64_Shdr> &sections, std::multimap<std::string, Elf64_Shdr> *allSections) {
    std::vector<Elf64_Shdr> shdrs;
    if (!ReadSectionHeaders(data, length, header, shdrs)) {
        ERROR_LOG("read elf section headers failed when load elf from buffer");
        return false;
    }
    // 各个 section 的名字保存在一个类型 StringTable 的 section 内(一般为 .shstrtab)
    // ELF 头部的 e_shstrndx 字段为这个 section 的序号，通过序号我们可以找到这个 section 头部
    const Elf64_Shdr &nameSecHeader = shdrs[header.e_shstrndx];
    std::vector<char> nameBuffer;
    if (!ReadArray<char>(data, length, nameSecHeader.sh_offset, nameSecHeader.sh_size, nameBuffer)) {
        ERROR_LOG("read header names failed when load elf from buffer");
        return false;
    }
    if (nameBuffer.empty() || nameBuffer.back() != '\0') {
        ERROR_LOG("section name table is not null-terminated.");
        return false;
    }
    for (const Elf64_Shdr &sectionHeader : shdrs) {
        if (sectionHeader.sh_name >= nameBuffer.size()) {
            ERROR_LOG("section name offset %u out of bounds.", sectionHeader.sh_name);
            return false;
        }
        std::string sectionName{nameBuffer.data() + sectionHeader.sh_name};
        sections[sectionName] = sectionHeader;
        if (allSections != nullptr) {
            allSections->emplace(sectionName, sectionHeader);
        }
    }
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

std::vector<char> Elf::ReadAllRawData(const std::string &name) const {
    std::vector<char> result;
    if (!elf::ReadAllRawData(buffer_.data(), buffer_.size(), allSections_, name, result)) {
        return {};
    }
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
    return elf::BuildSectionMap(buffer_.data(), buffer_.size(), header_, sections_, &allSections_);
}

bool ElfLoader::ScanSectionHeaders(const char *data, std::size_t length, std::map<std::string, Elf64_Shdr> &sections,
    std::multimap<std::string, Elf64_Shdr> *allSections) {
    sections.clear();
    if (allSections != nullptr) {
        allSections->clear();
    }
    if (data == nullptr || length == 0) {
        ERROR_LOG("empty elf data when parsing section headers");
        return false;
    }
    Elf64_Ehdr header{};
    if (!elf::ReadValue<Elf64_Ehdr>(data, length, 0, header)) {
        ERROR_LOG("Read elf header failed when parsing section headers");
        return false;
    }
    // 保留原严格校验：节头表项大小必须为 Elf64_Shdr
    if (header.e_shentsize != sizeof(Elf64_Shdr) || header.e_shoff == 0 || header.e_shnum == 0) {
        ERROR_LOG("Invalid ELF header when parsing section headers");
        return false;
    }
    if (!elf::ValidateElfHeader(header)) {
        ERROR_LOG("validate elf header failed when parsing section headers");
        return false;
    }
    if (!elf::ValidateElfHeaderSafety(header, length)) {
        return false;
    }
    return elf::BuildSectionMap(data, length, header, sections, allSections);
}

bool ElfLoader::LocateKernelSymtab(const char *data, std::size_t length, Elf64_Shdr &symtab, Elf64_Shdr &strtab,
    std::size_t &textIndex, bool &foundText) {
    foundText = false;
    textIndex = 0;
    if (data == nullptr || length == 0) {
        return false;
    }
    Elf64_Ehdr ehdr{};
    if (!elf::ReadValue<Elf64_Ehdr>(data, length, 0, ehdr)) {
        return false;
    }
    if (ehdr.e_shentsize != sizeof(Elf64_Shdr) || ehdr.e_shoff == 0 || ehdr.e_shnum == 0 ||
        ehdr.e_shstrndx >= ehdr.e_shnum) {
        DEBUG_LOG("invalid elf header when scanning kernel symbols");
        return false;
    }
    if (!elf::ValidateElfHeader(ehdr) || !elf::ValidateElfHeaderSafety(ehdr, length)) {
        return false;
    }
    std::vector<Elf64_Shdr> shdrs;
    if (!elf::ReadSectionHeaders(data, length, ehdr, shdrs)) {
        return false;
    }

    // 读取段名表（.shstrtab），仅用于定位 .text 段序号
    const Elf64_Shdr &nameSec = shdrs[ehdr.e_shstrndx];
    std::vector<char> nameBuffer;
    if (nameSec.sh_size == 0 || !elf::ReadArray<char>(data, length, nameSec.sh_offset, nameSec.sh_size, nameBuffer) ||
        nameBuffer.empty() || nameBuffer.back() != '\0') {
        return false;
    }

    std::size_t symtabIndex = 0;
    bool foundSymtab = false;
    for (std::size_t i = 0; i < shdrs.size(); ++i) {
        const Elf64_Shdr &sh = shdrs[i];
        if (sh.sh_type == SHT_SYMTAB && !foundSymtab) {
            symtabIndex = i;
            foundSymtab = true;
        }
        if (!foundText && sh.sh_name < nameBuffer.size() && std::strcmp(nameBuffer.data() + sh.sh_name, ".text") == 0) {
            textIndex = i;
            foundText = true;
        }
    }
    if (!foundSymtab) {
        DEBUG_LOG("no SHT_SYMTAB section found in kernel binary");
        return false;
    }

    symtab = shdrs[symtabIndex];
    if (symtab.sh_link >= ehdr.e_shnum) {
        return false;
    }
    strtab = shdrs[symtab.sh_link];
    if (symtab.sh_offset > length || symtab.sh_size > length - symtab.sh_offset || strtab.sh_offset > length ||
        strtab.sh_size > length - strtab.sh_offset) {
        return false;
    }
    return true;
}
