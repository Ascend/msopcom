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

#include <gtest/gtest.h>
#include <cstdlib>
#include "mockcpp/mockcpp.hpp"
#include "acl_rt_impl/HijackedFunc.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "runtime/inject_helpers/RegisterManager.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "utils/FileSystem.h"
#include "utils/ElfLoader.h"
#include "utils/Serialize.h"
#include "stub/FakeElf.h"
using namespace std;

static bool GetValidNameFromBinary(const char *data,
    uint64_t length,
    std::vector<std::string> &kernelNames,
    std::vector<uint64_t> &kernelOffsets)
{
    kernelNames.emplace_back("valid_kernel_1234_mix_aic");
    kernelNames.emplace_back("valid_kernel_1235_mix_aiv");
    kernelOffsets.emplace_back(0);
    kernelOffsets.emplace_back(1);
    return true;
}

static void CreateKernelJsonFile(std::string const &path, std::string const &content)
{
    std::ofstream ofs(path);
    ofs.write(content.c_str(), content.length());
}

class RegisterContextTest : public testing::Test {
public:
    void SetUp() override
    {
        AscendclImplOriginCtor();
        MOCKER(&GetSymInfoFromBinary).stubs().will(invoke(GetValidNameFromBinary));
        MOCKER(&HasStaticStub).stubs().will(returnValue(true));
        const char *path = "empty.o";
        uint32_t data{};
        vector<char> elfData(100, 1);
        aclrtBinHandle binHandle = &data;
        MOCKER_CPP(&ReadBinary).stubs().with(any(), outBound(elfData)).will(returnValue(size_t(1)));
        regCtx_ = RegisterManager::Instance().CreateContext(path, binHandle, RT_DEV_BINARY_MAGIC_ELF);
        ASSERT_NE(regCtx_, nullptr);
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        RegisterManager::Instance().Clear();
    }

    RegisterContextSP regCtx_;
};

TEST_F(RegisterContextTest, parse_magic_str_expect_return_corrent_magic_number)
{
    uint32_t magic{};
    ASSERT_TRUE(ParseMagicStr("RT_DEV_BINARY_MAGIC_ELF_AIVEC", magic));
    ASSERT_EQ(magic, RT_DEV_BINARY_MAGIC_ELF_AIVEC);
    ASSERT_TRUE(ParseMagicStr("RT_DEV_BINARY_MAGIC_ELF_AICUBE", magic));
    ASSERT_EQ(magic, RT_DEV_BINARY_MAGIC_ELF_AICUBE);
    ASSERT_TRUE(ParseMagicStr("RT_DEV_BINARY_MAGIC_ELF", magic));
    ASSERT_EQ(magic, RT_DEV_BINARY_MAGIC_ELF);
    ASSERT_TRUE(ParseMagicStr("RT_DEV_BINARY_MAGIC_ELF_AICPU", magic));
    ASSERT_EQ(magic, RT_DEV_BINARY_MAGIC_ELF_AICPU);
    ASSERT_FALSE(ParseMagicStr("UNKNOWN_MAGIC", magic));
}

TEST_F(RegisterContextTest, read_magic_from_not_exist_kernel_json_expect_return_false)
{
    std::string jsonPath("/tmp/not_exist.json");
    std::string magicStr;
    ASSERT_FALSE(ReadMagicFromKernelJson(jsonPath, magicStr));
}

TEST_F(RegisterContextTest, read_magic_when_json_parse_failed_expect_return_false)
{
    std::string jsonPath("/tmp/kernel.json");
    CreateKernelJsonFile(jsonPath, "{");
    std::string magicStr;
    ASSERT_FALSE(ReadMagicFromKernelJson(jsonPath, magicStr));
    std::remove(jsonPath.c_str());
}

TEST_F(RegisterContextTest, read_magic_from_json_without_magic_expect_return_false)
{
    std::string jsonPath("/tmp/kernel.json");
    CreateKernelJsonFile(jsonPath, "{}");
    std::string magicStr;
    ASSERT_FALSE(ReadMagicFromKernelJson(jsonPath, magicStr));
    std::remove(jsonPath.c_str());
}

TEST_F(RegisterContextTest, read_magic_from_json_with_valid_magic_expect_return_true)
{
    std::string jsonPath("/tmp/kernel.json");
    CreateKernelJsonFile(jsonPath, "{\"magic\":\"RT_DEV_BINARY_MAGIC_ELF_AIVEC\"}");
    ASSERT_TRUE(Chmod(jsonPath, 0640));
    std::string magicStr;
    ASSERT_TRUE(ReadMagicFromKernelJson(jsonPath, magicStr));
    ASSERT_EQ(magicStr, "RT_DEV_BINARY_MAGIC_ELF_AIVEC");
    std::remove(jsonPath.c_str());
}

TEST_F(RegisterContextTest, input_valid_tiling_key_then_get_kernel_name_expect_success)
{
    string expect = "valid_kernel_1234_mix_aic";
    ASSERT_EQ(regCtx_->GetKernelName(1234), expect);
}

TEST_F(RegisterContextTest, input_invalid_tiling_key_then_get_kernel_name_expect_fail)
{
    string expect = "";
    ASSERT_EQ(regCtx_->GetKernelName(0), expect);
}

TEST_F(RegisterContextTest, input_valid_kernel_name_then_get_offset_expect_success)
{
    string name = "valid_kernel_1235_mix_aiv";
    uint64_t expect = 1;
    ASSERT_EQ(regCtx_->GetKernelOffsetByName(name), expect);
}

TEST_F(RegisterContextTest, input_no_suffix_kernel_name_then_get_offset_expect_success)
{
    string name = "valid_kernel_1235";
    uint64_t expect = 1;
    ASSERT_EQ(regCtx_->GetKernelOffsetByName(name), expect);
}

TEST_F(RegisterContextTest, input_invalid_tiling_key_then_get_offset_expect_fail)
{
    string name = "abc";
    uint64_t expect = 0;
    ASSERT_EQ(regCtx_->GetKernelOffsetByName(name), expect);
}

TEST_F(RegisterContextTest, save_expect_true)
{
    MOCKER(&ElfLoader::LoadHeader).stubs().will(returnValue(true));
    EXPECT_TRUE(regCtx_->Save("empty path"));
}

TEST_F(RegisterContextTest, save_expect_false)
{
    MOCKER(&ElfLoader::LoadHeader).stubs().will(returnValue(true));
    MOCKER(&WriteBinary).stubs().will(returnValue(32));
    EXPECT_FALSE(regCtx_->Save("empty path"));
}

TEST_F(RegisterContextTest, Clone_expect_success)
{
    MOCKER(&IsExist).stubs().will(returnValue(true));
    MOCKER(&CopyFile).stubs().will(returnValue(true));
    EXPECT_NE(regCtx_->Clone("empty path4"), nullptr);
}

TEST_F(RegisterContextTest, mock_bin_file_then_call_clone_from_bin_expect_success)
{
    // create register context from data
    vector<char> elfData(100, 1);
    aclrtBinHandle binHandle = elfData.data();
    const void *data = static_cast<const void*>(elfData.data());
    auto ctx = RegisterManager::Instance().CreateContext(data, elfData.size(), binHandle, RT_DEV_BINARY_MAGIC_ELF, nullptr);
    EXPECT_NE(ctx->Clone("mock_path"), nullptr);
}

// 构造一个只包含 <shstrtab, metadata 段> 的合法 ELF，用于验证 GetMetaSection 的段名匹配逻辑
static Buffer CreateElfWithMetaSection(std::string const &metaSectionName, std::vector<uint8_t> const &metaData) {
    constexpr Elf64_Half SECTION_NUM = 2;
    Elf64_Ehdr header{};
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

    // .shstrtab 起始于 NUL 字符
    std::string strtab(1, '\0');
    strtab.append(".shstrtab").push_back('\0');
    strtab.append(metaSectionName).push_back('\0');
    const Elf64_Word strtabNameOff = 1;
    const Elf64_Word metaNameOff = 1 + strlen(".shstrtab") + 1;

    Elf64_Shdr strtabHeader{};
    strtabHeader.sh_name = strtabNameOff;
    strtabHeader.sh_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Shdr) * SECTION_NUM;
    strtabHeader.sh_size = strtab.size();

    Elf64_Shdr metaHeader{};
    metaHeader.sh_name = metaNameOff;
    metaHeader.sh_offset = strtabHeader.sh_offset + strtabHeader.sh_size;
    metaHeader.sh_size = metaData.size();

    Buffer buffer = Serialize<Buffer>(header) + Serialize<Buffer>(strtabHeader) + Serialize<Buffer>(metaHeader);
    buffer.insert(buffer.end(), strtab.begin(), strtab.end());
    buffer.insert(buffer.end(), metaData.begin(), metaData.end());
    return buffer;
}

TEST(RegisterContext, get_meta_section_with_exact_section_name_expect_success) {
    std::string sectionName = ".ascend.meta.test_kernel_mix_aiv";
    std::vector<uint8_t> metaData{0x11, 0x22, 0x33, 0x44};
    Buffer elf = CreateElfWithMetaSection(sectionName, metaData);

    rtDevBinary_t binary{};
    binary.data = elf.data();
    binary.length = elf.size();
    std::vector<uint8_t> parsed;
    ASSERT_EQ(GetMetaSection(binary, "test_kernel_mix_aiv", parsed), metaData.size());
    ASSERT_EQ(parsed, metaData);
}

TEST(RegisterContext, get_meta_section_with_mix_aiv_tail_expect_success) {
    // 回归用例：MIX 二进制的 meta 段名带 _mix_aiv 尾缀，而 kernelName 不带该尾缀，
    // 验证 GetMetaSection 能通过尾缀变体匹配到 meta 段
    std::string sectionName = ".ascend.meta.test_kernel_mix_aiv";
    std::vector<uint8_t> metaData{0x1, 0x2, 0x3, 0x4, 0x5};
    Buffer elf = CreateElfWithMetaSection(sectionName, metaData);

    rtDevBinary_t binary{};
    binary.data = elf.data();
    binary.length = elf.size();
    std::vector<uint8_t> parsed;
    ASSERT_EQ(GetMetaSection(binary, "test_kernel", parsed), metaData.size());
    ASSERT_EQ(parsed, metaData);
}

TEST(RegisterContext, get_meta_section_with_mix_aic_tail_expect_success) {
    // 回归用例：MIX 二进制的 meta 段名带 _mix_aic 尾缀，而 kernelName 不带该尾缀，
    // 验证 GetMetaSection 能通过尾缀变体匹配到 meta 段
    std::string sectionName = ".ascend.meta.test_kernel_mix_aic";
    std::vector<uint8_t> metaData{0x9, 0x8, 0x7, 0x6, 0x5, 0x4};
    Buffer elf = CreateElfWithMetaSection(sectionName, metaData);

    rtDevBinary_t binary{};
    binary.data = elf.data();
    binary.length = elf.size();
    std::vector<uint8_t> parsed;
    ASSERT_EQ(GetMetaSection(binary, "test_kernel", parsed), metaData.size());
    ASSERT_EQ(parsed, metaData);
}

TEST(RegisterContext, get_meta_section_with_nonexistent_section_expect_return_zero) {
    std::string sectionName = ".ascend.meta.other_kernel";
    std::vector<uint8_t> metaData{0x1};
    Buffer elf = CreateElfWithMetaSection(sectionName, metaData);

    rtDevBinary_t binary{};
    binary.data = elf.data();
    binary.length = elf.size();
    std::vector<uint8_t> parsed;
    ASSERT_EQ(GetMetaSection(binary, "missing_kernel", parsed), 0);
    ASSERT_TRUE(parsed.empty());
}
