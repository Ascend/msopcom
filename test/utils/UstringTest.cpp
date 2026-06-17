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


#include "utils/Ustring.h"

#include <string>
#include <vector>
#include <gtest/gtest.h>

TEST(Ustring, digit_is_negative_number)
{
    std::string output;
    ASSERT_TRUE(IsDigit("-1"));
}

TEST(Ustring, not_end_with) {
    std::string fullStr = "HelloWorld";
    std::string ending = "Hello";

    EXPECT_FALSE(EndsWith(fullStr, ending));
}

TEST(Ustring, empty_ending) {
    std::string fullStr = "HelloWorld";
    std::string ending = "";
    EXPECT_TRUE(EndsWith(fullStr, ending));
}

TEST(Ustring, empty_full_str) {
    std::string fullStr = "";
    std::string ending = "World";
    EXPECT_FALSE(EndsWith(fullStr, ending));
}

TEST(Ustring, full_str_shorter_than_ending) {
    std::string fullStr = "Hi";
    std::string ending = "Hello";
    EXPECT_FALSE(EndsWith(fullStr, ending));
}

TEST(Ustring, case_sensitive) {
    std::string fullStr = "HelloWorld";
    std::string ending = "world";
    EXPECT_FALSE(EndsWith(fullStr, ending));
}

TEST(Ustring, handles_zero)
{
    EXPECT_EQ(NumToHexString(0), "0x0");
}

TEST(Ustring, handles_positive_number)
{
    EXPECT_EQ(NumToHexString(255), "0xff");
    EXPECT_EQ(NumToHexString(4096), "0x1000");
}

TEST(Ustring, handles_max_uint64)
{
    uint64_t max_value = std::numeric_limits<uint64_t>::max();
    EXPECT_EQ(NumToHexString(max_value), "0xffffffffffffffff");
}

TEST(Ustring, double_test_fail) {
    std::string str = "123.45";
    double num;
    // StringToNum doesn't support double or float convert because include '.'
    EXPECT_FALSE(StringToNum<double>(str, num));
}

TEST(Ustring, invalid_input_test) {
    std::string str = "abc";
    int num;
    EXPECT_FALSE(StringToNum<int>(str, num));
}

TEST(Ustring, empty_string_test) {
    std::string str = "";
    int num;
    EXPECT_FALSE(StringToNum<int>(str, num));
}

TEST(Ustring, digit_is_number)
{
    std::string output;
    ASSERT_TRUE(IsDigit("111"));
    int a;
    ASSERT_TRUE(StoiConverter("111", a));
}
TEST(Ustring, not_a_digit)
{
    std::string output;
    ASSERT_FALSE(IsDigit("aa"));
    int a;
    ASSERT_FALSE(StoiConverter("aa", a));
}
TEST(Ustring, stoi_out_of_range)
{
    int a;
    ASSERT_FALSE(StoiConverter(std::to_string(UINT64_MAX), a));
}
TEST(Ustring, split_string)
{
    std::string output = "aa,bb,cc";
    std::set<std::string> res;
    SplitString(output, ',', res);
    EXPECT_EQ(*res.begin(), "aa");
}
TEST(Ustring, check_input_string_valid)
{
    ASSERT_FALSE(CheckInputStringValid("", 10));
    std::string output = "~~~~";
    ASSERT_FALSE(CheckInputStringValid(output, 1));
    ASSERT_FALSE(CheckInputStringValid(output, 4));
    output = "AAAA";
    ASSERT_TRUE(CheckInputStringValid(output, 4));
}

TEST(Ustring, check_remove_suffix)
{
    constexpr char const *MIX_AIC_TAIL = "_mix_aic";
    constexpr char const *MIX_AIV_TAIL = "_mix_aiv";
    std::string hasSuffix1 = "mat_mul_mix_aic";
    std::string hasSuffix2 = "mat_mul_mix_aiv";
    RemoveSuffix(hasSuffix1, {MIX_AIC_TAIL, MIX_AIV_TAIL});
    RemoveSuffix(hasSuffix2, {MIX_AIC_TAIL, MIX_AIV_TAIL});
    std::string noSuffix1 = "mat_mul_mix_aiC";
    std::string noSuffix2 = "mat_mul_mix_aiV";
    RemoveSuffix(noSuffix1, {MIX_AIC_TAIL, MIX_AIV_TAIL});
    RemoveSuffix(noSuffix2, {MIX_AIC_TAIL, MIX_AIV_TAIL});
    EXPECT_EQ(hasSuffix1, "mat_mul");
    EXPECT_EQ(hasSuffix2, "mat_mul");
    EXPECT_EQ(noSuffix1, "mat_mul_mix_aiC");
    EXPECT_EQ(noSuffix2, "mat_mul_mix_aiV");
}

/**
 * | 用例集 | UstringTest
 * |测试函数| ToLower()
 * | 用例名 | test_to_lower_correct
 * |用例描述| 测试转大写字母字符串正确
 */
TEST(Ustring, test_to_lower_correct)
{
    std::string str = "HeLLo WoRLd";
    ToLower(str);
    EXPECT_EQ(str, "hello world");
    str = "ABC123_XYZ";
    ToLower(str);
    EXPECT_EQ(str, "abc123_xyz");
}
