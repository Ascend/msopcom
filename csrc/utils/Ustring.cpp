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


#include "Ustring.h"
#include <algorithm>

bool IsDigit(std::string const &digit)
{
    uint32_t index = 0;
    if ((digit.length() > 0) && (digit[0] == '-' || digit[0] == '+')) {
        index = 1;
    }
    if ((digit.length() == index) || (digit.length() > UINT32_MAX)) {
        return false;
    }
    for (;index < digit.length(); index++) {
        if (!isdigit(digit[index])) {
            return false;
        }
    }
    return true;
}

bool CheckInputStringValid(std::string const &inputString, uint64_t length)
{
    if (inputString.empty()) {
        return false;
    }
    if (inputString.length() > length) {
        return false;
    }
    for (char c : inputString) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
            return false;
        }
    }
    return true;
}

void SplitString(const std::string &str, const char split, std::set<std::string> &res)
{
    std::istringstream iss(str);
    std::string token;
    while (getline(iss, token, split)) {
        res.insert(token);
    }
}

void SplitString(const std::string &str, const char split, std::vector<std::string> &res)
{
    std::istringstream iss(str);
    std::string token;
    res.clear();
    while (getline(iss, token, split)) {
        res.emplace_back(token);
    }
}

std::vector<char *> ToRawCArgv(std::vector<std::string> const &argv)
{
    std::vector<char *> rawArgv;
    for (auto const &arg: argv) {
        rawArgv.emplace_back(const_cast<char *>(arg.data())); // NOLINT(cppcoreguidelines-pro-type-const-cast)
    }
    rawArgv.emplace_back(nullptr);
    return rawArgv;
}

uint64_t GetValidLength(const char *str, uint64_t maxLen)
{
    if (str == nullptr) {
        return 0;
    }
    auto end = std::find(str, str + maxLen, '\0');
    auto count = static_cast<uint64_t>(std::distance(str, end));
    return std::min(count, maxLen - 1);
}

bool IsStringCharValid(const std::string &inputString, std::string &msg)
{
    const std::unordered_map<std::string, std::string> invalidChar = GetInvalidChar();
    for (auto &item: invalidChar) {
        if (inputString.find(item.first) != std::string::npos) {
            msg = "invalid character: " + item.second;
            return false;
        }
    }
    return true;
}
void RemoveSuffix(std::string& str, const std::vector<std::string>& suffixes)
{
    if (str.empty()) {
        return;
    }
    for (const auto& suffix : suffixes) {
        size_t suffixLen = suffix.length();
        if (str.length() >= suffixLen && str.compare(str.length() - suffixLen, suffixLen, suffix) == 0) {
            str.erase(str.length() - suffixLen);
            break;
        }
    }
}

std::string ExtractObjName(const std::string& str, const std::string& prefix, const std::string& suffix)
{
    size_t prefixIndex = str.find(prefix);
    size_t suffixIndex = str.find(suffix);
    if (prefixIndex == std::string::npos || suffixIndex == std::string::npos) {
        return "";
    }
    size_t objStart = prefixIndex + prefix.length();
    size_t objLength = suffixIndex - objStart;
    return str.substr(objStart, objLength);
}
