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

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <csignal>
#include <unistd.h>
#include "Communication.h"

class LocalDevice;

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
    explicit LocalProcess(CommType type, bool isDeviceConnection = false);
    static LocalProcess &GetInstance(CommType type = CommType::SOCKET);
    ~LocalProcess();
    void Log(LogLevel level, std::string msg) const;
    // 通用通知直接发送，设备业务包在当前连接锁内取号后发送
    ssize_t Notify(std::string msg) const;

    // 上报算子二进制，设备连接只取一次号，fd 路径不可用时沿用该号回退字节流
    ssize_t NotifyKernelBinary(std::vector<char> const &elfData) const;
    ssize_t Wait(std::string& msg, uint32_t timeOut = 10) const;
    int TerminateWithSignal(int signo = SIGINT) const;

protected:
    // 设备连接表和编号源属于整个 Host 进程，由所有设备连接共享
    struct ProcessState {
        const pid_t ownerPid{getpid()};
        std::mutex devicesMutex; // 防止 devices 在多线程下被同时修改
        std::unordered_map<int32_t, std::unique_ptr<LocalDevice>> devices; // 当前 Host 进程的设备连接对象表
        std::atomic<uint64_t> nextOrderId{1};
    };
    // 首次访问时在 LocalProcess.cpp 中创建进程共享状态
    static ProcessState &State();
    // 发包加锁前检查 PID，防止 fork 子进程等待父进程遗留的设备锁
    static void CheckProcessOwner();
    // 从当前 Host 进程的共享编号源取一个业务号；0 表示编号耗尽
    static uint64_t ReserveOrderId();

private:
    // 创建二进制传输用的内存文件；不支持时由发送入口回退到字节流
    static int CreateKernelMemfd(const char *name);
    // 只发送已有字节，不取号、不加锁，二进制回退不会再次进入业务包入口
    ssize_t SendBytes(std::string const &msg) const;

    const bool isDeviceConnection_; // 仅设备业务连接需要进程编号，配置和通用文本通知不解析包头，不能加编号
    mutable std::mutex sendMutex_; // 保护当前连接的完整设备包发送，不是进程共享锁
    Client* client_ = nullptr;
}; // class LocalProcess

ssize_t Notify(std::string message);

ssize_t Wait(std::string& msg);

void DebugLog(std::string message);

void InfoLog(std::string message);

void WarnLog(std::string message);

void ErrorLog(std::string message);

#endif // __CORE_LOCAL_PROCESS_H__
