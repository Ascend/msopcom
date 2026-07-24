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


#ifndef __UTILS_INJECT_LOGGER_H__
#define __UTILS_INJECT_LOGGER_H__

#include <cstdarg>
#include <cstdio>
#include <string>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <mutex>

#include "utils/FileSystem.h"
#include "utils/Ustring.h"

enum class LogLv : uint8_t {
    VERBOSE = 0U,
    DEBUG,
    INFO,
    WARN,
    ERROR,
};

class InjectLogger {
public:
    static inline InjectLogger &Instance();
    ~InjectLogger() = default;
    InjectLogger(InjectLogger const &) = delete;
    InjectLogger &operator=(InjectLogger const &) = delete;

#if defined (__GNUC__)
    // 使用 gcc 的内置能力检查格式化输出的参数类型。format 属性接受 3 个参数：
    // 1. archetype: 按照哪种风格进行校验，此处选用 printf
    // 2. string-index: 格式化 format 字符串参数在函数入参中的索引
    // 3. first-to-check: 第一个要检查的可变参数在函数入参中的索引
    // 注意：Log 函数为成员函数，存在一个隐藏的 this 指针参数，参数与索引的对应关系为：
    // 1: this, 2: priority, 3: format, 4: vargs
    // 因此此处两个索引值分别为 3, 4
    __attribute__((format(printf, 3, 4)))
#endif
    inline void Log(const LogLv priority, char const *__restrict format, ...) const;

    LogLv GetLogLv() const;

    static constexpr std::size_t MAX_LOG_BUF_SIZE = 2048UL;
    char logBuf_[MAX_LOG_BUF_SIZE] = {0};

private:
    InjectLogger()
    {
        static const std::string INJ_LOG_LEVEL = "INJ_LOG_LEVEL";
        auto lvStr = GetEnv(INJ_LOG_LEVEL);
        static const std::map<std::string, LogLv> LOG_LEVEL_MAP = {
            {"0", LogLv::VERBOSE},
            {"1", LogLv::DEBUG},
            {"2", LogLv::INFO},
            {"3", LogLv::WARN},
            {"4", LogLv::ERROR},
        };
        auto it = LOG_LEVEL_MAP.find(lvStr);
        if (it != LOG_LEVEL_MAP.end()) {
            lv_ = it->second;
        }
    }

    std::ostringstream streamBuffer_;
    LogLv lv_ {LogLv::INFO};
};

inline InjectLogger &InjectLogger::Instance()
{
    static InjectLogger instance;
    return instance;
}

inline void InjectLogger::Log(const LogLv priority, char const *__restrict format, ...) const
{
    if (static_cast<uint8_t>(priority) < static_cast<uint8_t>(lv_)) {
        return;
    }
    static std::mutex mtx;
    std::string safeFormat = ToSafeString(format);
    // todo: 优先把日志发送回server，若发送失败，则标准输出
    va_list args;
    va_start(args, format);
    {
        std::lock_guard<std::mutex> lock(mtx);
        vfprintf(stdout, (safeFormat + "\n").c_str(), args);
        fflush(stdout);
    }
    va_end(args);
}

inline LogLv InjectLogger::GetLogLv() const
{
    return lv_;
}

#define ERROR_LOG(format, ...) \
    InjectLogger::Instance().Log(LogLv::ERROR, "[ERROR] <%s> " format, __func__, ## __VA_ARGS__)
#define WARN_LOG(format, ...) \
    InjectLogger::Instance().Log(LogLv::WARN, "[WARN] <%s> " format, __func__, ## __VA_ARGS__)
#define INFO_LOG(format, ...) \
    InjectLogger::Instance().Log(LogLv::INFO, "[INFO] <%s> " format, __func__, ## __VA_ARGS__)
#define DEBUG_LOG(format, ...) \
    InjectLogger::Instance().Log(LogLv::DEBUG, "[DEBUG] <%s> " format, __func__, ##__VA_ARGS__)
#define VERBOSE_LOG(format, ...) \
    InjectLogger::Instance().Log(LogLv::VERBOSE, "[VERBOSE] <%s> " format, __func__, ##__VA_ARGS__)

#endif // __UTILS_INJECT_LOGGER_H__
