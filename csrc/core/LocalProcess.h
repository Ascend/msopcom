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


#ifndef __CORE_LOCAL_PROCESS_H__
#define __CORE_LOCAL_PROCESS_H__

#include <cstdint>
#include <string>
#include <vector>
#include <csignal>
#include "Communication.h"

enum class LogLevel : uint8_t {
    DEBUG = 0U,
    INFO,
    WARN,
    ERROR,
};

// LocalProcess类是本进程的抽象，由于桩函数本身在用户进程中，对于这个进程的操作提供公共能力
// 由于工具的本体在远端进程中，所以，LocalProcess会利用通信实现一些行为抽象
// 由于应用本身存在跨进程的情况，需要抽象适配本进程行为。
// 具体行为包括：
// 1. 日志操作
// 2. 事件通知
// 特别说明：
// 1. 多进程场景下每个进程实例化一个 LocalProcess 实例进行连接，服务端由 ClientId 区分客户端
// 2. 多线程场景下各线程可以共用 LocalProcess 实例，由协议层进行线程的区分
class LocalProcess {
public:
    explicit LocalProcess(CommType type);
    static LocalProcess &GetInstance(CommType type = CommType::SOCKET);
    ~LocalProcess();
    void Log(LogLevel level, std::string msg) const;
    ssize_t Notify(std::string const &msg) const;

    /** 上报内核二进制：优先走 memfd + SCM_RIGHTS 零拷贝 fd 路径，失败回退字节流 */
    ssize_t NotifyKernelBinary(std::vector<char> const &elfData) const;
    ssize_t Wait(std::string& msg, uint32_t timeOut = 10) const;
    int TerminateWithSignal(int signo = SIGINT) const;

private:
    Client* client_ = nullptr;
}; // class LocalProcess

ssize_t Notify(std::string message);

ssize_t Wait(std::string& msg);

void DebugLog(std::string message);

void InfoLog(std::string message);

void WarnLog(std::string message);

void ErrorLog(std::string message);

#endif // __CORE_LOCAL_PROCESS_H__
