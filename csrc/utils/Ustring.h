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


#ifndef __USTRING_H__
#define __USTRING_H__

#include <cstdint>
#include <string>
#include <set>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <unordered_map>

constexpr char const *MC2_AICPU_SUFFIX = {"Mc2AicpuKernel"};
constexpr char const *MIX_AIC_SUFFIX = "_mix_aic";
constexpr char const *MIX_AIV_SUFFIX = "_mix_aiv";

inline bool StartsWith(const std::string &str, const std::string &prefix)
{
    return (str.rfind(prefix, 0) == 0);
}

// move to string utils
inline bool EndsWith(const std::string &fullStr, const std::string &ending)
{
    if (fullStr.length() >= ending.length()) {
        return fullStr.compare(fullStr.length() - ending.length(), ending.length(), ending) == 0;
    }
    return false;
}

inline bool StoiConverter(const std::string &numString, int &num, int radix = 10)
{
    try {
        num = stoi(numString, nullptr, radix);
    } catch (std::invalid_argument&) {
        return false;
    } catch (std::out_of_range&) {
        return false;
    }
    return true;
}

inline std::string NumToHexString(uint64_t num)
{
    std::stringstream ss;
    ss << "0x" << std::hex << num;
    return ss.str();
}

bool IsDigit(std::string const &digit);

bool CheckInputStringValid(std::string const &inputString, uint64_t length);

void SplitString(const std::string &str, const char split, std::set<std::string> &res);

void SplitString(const std::string &str, const char split, std::vector<std::string> &res);

std::vector<char *> ToRawCArgv(std::vector<std::string> const &argv);

uint64_t GetValidLength(const char *str, uint64_t maxLen);

bool IsStringCharValid(const std::string &inputString, std::string &msg);

void RemoveSuffix(std::string& str, const std::vector<std::string>& suffixes);

std::string ExtractObjName(const std::string& str, const std::string& prefix, const std::string& suffix);

template <class Type>
inline bool StringToNum(const std::string& str, Type &number)
{
    if (!IsDigit(str)) {
        return false;
    }
    // 对无符号类型拒绝负数输入
    if (std::is_unsigned<Type>::value) {
        if (!str.empty() &&str[0] == '-') {
            return false;
        }
    }
    std::istringstream iss(str);
    Type num;
    iss >> num;
    if (iss.fail()) {
        return false;
    }
    number = num;
    return true;
}

template <typename T,
    typename std::enable_if <std::is_same<T, std::chrono::seconds>::value ||
                             std::is_same<T, std::chrono::milliseconds>::value ||
                             std::is_same<T, std::chrono::nanoseconds>::value >::type* = nullptr>
std::string GenerateTimeStamp()
{
    char buf[32] = {0};
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm *tm = std::localtime(&time);
    std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", tm);
    std::string ts = buf;
    if (std::is_same<T, std::chrono::milliseconds>::value) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        std::stringstream ss;
        ss << std::setw(3) << std::setfill('0') << (ms % 1000); // 保证毫秒是3位数,需要对1000取模
        ts += ss.str();
    } else if (std::is_same<T, std::chrono::nanoseconds>::value) {
        auto ms = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
        std::stringstream ss;
        ss << std::setw(9) << std::setfill('0') << (ms % 1000000000); // 保证纳秒是9位数,需要对1000000000取模
        ts += ss.str();
    }
    return ts;
}

// 避免访问析构后的静态变量导致程序异常退出
inline std::unordered_map<std::string, std::string> GetInvalidChar()
{
    return {
            {"\n", "\\n"}, {"\f", "\\f"}, {"\r", "\\r"}, {"\b", "\\b"},
            {"\t", "\\t"}, {"\v", "\\v"}, {"\u007F", "\\u007F"}
    };
}

// convert unsafe string to safe string
inline std::string ToSafeString(const std::string &str)
{
    std::string safeStr(str);
    std::unordered_map<std::string, std::string> invalidChar = GetInvalidChar();
    size_t i = 0;
    while (i < safeStr.length()) {
        std::string chr(1, safeStr[i]);
        if (invalidChar.find(chr) != invalidChar.end()) {
            auto &validStr = invalidChar.at(chr);
            safeStr.replace(i, 1, validStr);
            i += validStr.length();
            continue;
        }
        i++;
    }
    return safeStr;
}

// 为日志打印格式化共享内存名称: 直接输出 32 位 FNV-1a 哈希, 形如 hash=5020ab2f,
// 既避免二进制 name 造成的乱码, 也避免长串 \\xHH 转义污染日志; 同一 name 哈希值确定, 便于跨日志行关联
inline std::string FormatNameForLog(const std::string &str) {
    if (str.empty()) {
        return {};
    }
    static const char HEX_DIGITS[] = "0123456789abcdef";
    uint32_t hash = 0x811c9dc5U; // FNV-1a 32-bit basis
    for (unsigned char c : str) {
        hash ^= c;
        hash *= 0x01000193U; // FNV-1a 32-bit prime
    }
    std::string out = "hash=";
    char hex[9] = {0}; // 8 hex digits + NUL
    for (int i = 7; i >= 0; --i) {
        hex[i] = HEX_DIGITS[hash & 0xFU];
        hash >>= 4U;
    }
    out += hex;
    return out;
}

template<typename Iterator>
inline std::string Join(Iterator beg, Iterator end, std::string const &sep = " ")
{
    std::string ret;
    if (beg == end) {
        return ret;
    }
    ret = *beg++;
    for (; beg != end; ++beg) {
        ret.append(sep);
        ret.append(*beg);
    }
    return ret;
}

inline std::string Strip(std::string str, std::string const &cs = " ")
{
    std::string::size_type l = str.find_first_not_of(cs);
    std::string::size_type r = str.find_last_not_of(cs);
    return l == std::string::npos || r == std::string::npos ? "" : str.substr(l, r - l + 1);
}

inline void ToLower(std::string &s) {
    for (auto& c : s) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
}
#endif // __USTRING_H__
