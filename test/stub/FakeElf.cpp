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


#include "utils/Serialize.h"

#include "FakeElf.h"

#include <algorithm>
#include <cstddef>

constexpr Elf64_Half SECTION_NUM = 2;
constexpr Elf64_Xword SECTION_SIZE = 18;
constexpr Elf64_Word SHSTRTAB_OFFSET = 1;
constexpr Elf64_Word DUMMY_OFFSET = 11;

Elf64_Ehdr CreateValidElfHeader(void)
{
    Elf64_Ehdr header {};
    header.e_ident[EI_MAG0] = ELFMAG0;
    header.e_ident[EI_MAG1] = ELFMAG1;
    header.e_ident[EI_MAG2] = ELFMAG2;
    header.e_ident[EI_MAG3] = ELFMAG3;
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_shnum = SECTION_NUM;
    header.e_shstrndx = 0;
    header.e_shentsize = sizeof(Elf64_Shdr);
    header.e_shoff = sizeof(Elf64_Ehdr);
    return header;
}

Buffer CreateValidNameSection(void)
{
    std::vector<std::string> sectionNames = {
        ".shstrtab",
        ".dummy"
    };
    // string table starts with NUL
    Buffer section(1, '\0');
    for (auto const &name : sectionNames) {
        std::copy(name.cbegin(), name.cend(), std::back_inserter(section));
        section.push_back('\0');
    }
    return section;
}

Buffer CreateValidElf(void)
{
    Elf64_Ehdr elfHeader = CreateValidElfHeader();
    Elf64_Shdr nameSectionHeader {};
    nameSectionHeader.sh_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Shdr) + sizeof(Elf64_Shdr);
    nameSectionHeader.sh_size = SECTION_SIZE;
    nameSectionHeader.sh_name = SHSTRTAB_OFFSET;
    Elf64_Shdr dummySectionHeader {};
    dummySectionHeader.sh_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Shdr) + sizeof(Elf64_Shdr);
    dummySectionHeader.sh_size = SECTION_SIZE;
    dummySectionHeader.sh_name = DUMMY_OFFSET;
    return Serialize<Buffer>(elfHeader) + Serialize<Buffer>(nameSectionHeader) +
        Serialize<Buffer>(dummySectionHeader) + CreateValidNameSection();
}

std::vector<uint8_t> CreateElfWithKernelSymbols(const std::vector<std::string> &kernelNames,
    const std::vector<std::string> &simtNames, bool strtabAtEnd, bool lastSimtNoNul) {
    constexpr Elf64_Word TEXT_IDX = 1;
    constexpr Elf64_Word SYMTAB_IDX = 2;
    constexpr Elf64_Word STRTAB_IDX = 3;
    constexpr Elf64_Word SHSTRTAB_IDX = 4;
    constexpr Elf64_Half SHNUM = 5;

    std::string shstrtab(1, '\0');
    const Elf64_Word textName = static_cast<Elf64_Word>(shstrtab.size());
    shstrtab += ".text";
    shstrtab.push_back('\0');
    const Elf64_Word symtabName = static_cast<Elf64_Word>(shstrtab.size());
    shstrtab += ".symtab";
    shstrtab.push_back('\0');
    const Elf64_Word strtabName = static_cast<Elf64_Word>(shstrtab.size());
    shstrtab += ".strtab";
    shstrtab.push_back('\0');
    const Elf64_Word shstrtabName = static_cast<Elf64_Word>(shstrtab.size());
    shstrtab += ".shstrtab";
    shstrtab.push_back('\0');

    // .strtab 及符号表；索引 0 为保留的空符号
    std::string strtab(1, '\0');
    std::vector<Elf64_Sym> syms(1);
    for (std::size_t i = 0; i < kernelNames.size(); ++i) {
        Elf64_Sym sym{};
        sym.st_name = static_cast<Elf64_Word>(strtab.size());
        sym.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
        sym.st_shndx = TEXT_IDX;
        sym.st_value = 0x10 * (i + 1);
        strtab += kernelNames[i];
        strtab.push_back('\0');
        syms.push_back(sym);
    }
    for (std::size_t i = 0; i < simtNames.size(); ++i) {
        Elf64_Sym sym{};
        sym.st_name = static_cast<Elf64_Word>(strtab.size());
        sym.st_info = ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE);
        sym.st_shndx = TEXT_IDX;
        strtab += simtNames[i];
        if (!(lastSimtNoNul && i + 1 == simtNames.size())) {
            strtab.push_back('\0');
        }
        syms.push_back(sym);
    }
    const std::size_t symtabSize = syms.size() * sizeof(Elf64_Sym);
    std::vector<uint8_t> text(16, 0);

    Elf64_Ehdr header{};
    header.e_ident[EI_MAG0] = ELFMAG0;
    header.e_ident[EI_MAG1] = ELFMAG1;
    header.e_ident[EI_MAG2] = ELFMAG2;
    header.e_ident[EI_MAG3] = ELFMAG3;
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_type = ET_REL;
    header.e_ehsize = sizeof(Elf64_Ehdr);
    header.e_shnum = SHNUM;
    header.e_shstrndx = SHSTRTAB_IDX;
    header.e_shentsize = sizeof(Elf64_Shdr);
    header.e_shoff = sizeof(Elf64_Ehdr);

    Elf64_Off cursor = sizeof(Elf64_Ehdr) + static_cast<Elf64_Off>(SHNUM) * sizeof(Elf64_Shdr);
    const Elf64_Off textOff = cursor;
    cursor += text.size();
    const Elf64_Off symtabOff = cursor;
    cursor += symtabSize;
    Elf64_Off strtabOff = 0;
    Elf64_Off shstrtabOff = 0;
    if (strtabAtEnd) {
        shstrtabOff = cursor;
        cursor += shstrtab.size();
        strtabOff = cursor;
        cursor += strtab.size();
    } else {
        strtabOff = cursor;
        cursor += strtab.size();
        shstrtabOff = cursor;
        cursor += shstrtab.size();
    }

    std::vector<Elf64_Shdr> shdrs(SHNUM);
    shdrs[TEXT_IDX].sh_name = textName;
    shdrs[TEXT_IDX].sh_type = SHT_PROGBITS;
    shdrs[TEXT_IDX].sh_offset = textOff;
    shdrs[TEXT_IDX].sh_size = text.size();
    shdrs[TEXT_IDX].sh_addralign = 1;

    shdrs[SYMTAB_IDX].sh_name = symtabName;
    shdrs[SYMTAB_IDX].sh_type = SHT_SYMTAB;
    shdrs[SYMTAB_IDX].sh_offset = symtabOff;
    shdrs[SYMTAB_IDX].sh_size = symtabSize;
    shdrs[SYMTAB_IDX].sh_link = STRTAB_IDX;
    shdrs[SYMTAB_IDX].sh_info = 1;
    shdrs[SYMTAB_IDX].sh_entsize = sizeof(Elf64_Sym);

    shdrs[STRTAB_IDX].sh_name = strtabName;
    shdrs[STRTAB_IDX].sh_type = SHT_STRTAB;
    shdrs[STRTAB_IDX].sh_offset = strtabOff;
    shdrs[STRTAB_IDX].sh_size = strtab.size();

    shdrs[SHSTRTAB_IDX].sh_name = shstrtabName;
    shdrs[SHSTRTAB_IDX].sh_type = SHT_STRTAB;
    shdrs[SHSTRTAB_IDX].sh_offset = shstrtabOff;
    shdrs[SHSTRTAB_IDX].sh_size = shstrtab.size();

    std::vector<uint8_t> elf(static_cast<std::size_t>(cursor));
    uint8_t *p = elf.data();
    std::copy_n(reinterpret_cast<const uint8_t *>(&header), sizeof(header), p);
    p += sizeof(header);
    std::copy_n(reinterpret_cast<const uint8_t *>(shdrs.data()), shdrs.size() * sizeof(Elf64_Shdr), p);
    p += shdrs.size() * sizeof(Elf64_Shdr);
    std::copy_n(text.data(), text.size(), p);
    p += text.size();
    std::copy_n(reinterpret_cast<const uint8_t *>(syms.data()), symtabSize, p);
    p += symtabSize;
    if (strtabAtEnd) {
        std::copy(shstrtab.cbegin(), shstrtab.cend(), p);
        p += shstrtab.size();
        std::copy(strtab.cbegin(), strtab.cend(), p);
    } else {
        std::copy(strtab.cbegin(), strtab.cend(), p);
        p += strtab.size();
        std::copy(shstrtab.cbegin(), shstrtab.cend(), p);
    }
    return elf;
}
