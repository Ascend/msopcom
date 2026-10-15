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

// syscall 声明依赖 _GNU_SOURCE；部分构建环境的编译器不会默认定义，这里显式声明。
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "LocalProcess.h"

#include <map>
#include <thread>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#include "FuncSelector.h"
#include "utils/InjectLogger.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"

namespace {
// memfd_create 包装函数依赖较新的 glibc（>=2.27）；这里直接走 syscall，
// 兼容较老的构建镜像。内核不支持时返回 -1，由调用方回退到字节流发送。
int CreateKernelMemfd(const char *name) {
#if defined(SYS_memfd_create)
    return static_cast<int>(syscall(SYS_memfd_create, name, MFD_CLOEXEC));
#else
    (void)name;
    return -1;
#endif
}

bool WriteAllToFd(int fd, char const *data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        ssize_t written = write(fd, data + offset, length - offset);
        if (written <= 0) {
            return false;
        }
        offset += static_cast<size_t>(written);
    }
    return true;
}
} // namespace

LocalProcess::LocalProcess(CommType type)
{
    constexpr int retryLimit = 50;            // 链接充实次数上限为50
    client_ = new Client(type); // NOLINT(cppcoreguidelines-owning-memory)
    int retryTimes = 0;
    while (client_ != nullptr && !client_->Connect() && retryTimes <= retryLimit) {
        // server 启动前 client 会连接失败，等待 100ms 后重试
        constexpr uint64_t connectRetryDuration = 100;
        std::this_thread::sleep_for(std::chrono::milliseconds(connectRetryDuration));
        retryTimes++;
    }
    if (retryTimes > retryLimit) {
        ERROR_LOG("connect server failed");
        return;
    }
    DEBUG_LOG("connect server success.");
}

LocalProcess &LocalProcess::GetInstance(CommType type)
{
    static LocalProcess instance(type);
    return instance;
}

LocalProcess::~LocalProcess()
{
    if (client_ != nullptr) {
        delete client_;
        client_ = nullptr;
    }
}

void LocalProcess::Log(LogLevel level, std::string msg) const
{
    static std::map<LogLevel, std::string> levelStrMap = {
        {LogLevel::DEBUG, "[LOG][DEBUG]"},
        {LogLevel::INFO, "[LOG][INFO]"},
        {LogLevel::WARN, "[LOG][WARN]"},
        {LogLevel::ERROR, "[LOG][ERROR]"}
    };
    // 需要设计一个结构支持合理分发，此处简单拼接下 [LOG][DEBUG] rtDevBinaryRegister Hijacked bin is nullptr.;
    std::string m = levelStrMap[level] + " " + msg + ";";
    if (client_ != nullptr) {
        client_->Write(m);
    }
}

// 通过工具侧配合wait实现阻塞功能
// 此处Notify的消息格式为[MESSAGE] xxx:xxx; 与log格式统一，降低工具侧消息解析复杂度
ssize_t LocalProcess::Notify(std::string const &msg) const
{
    ssize_t sentBytes = 0;
    size_t totalSent = 0;
    while (totalSent < msg.size() && client_ != nullptr) {
        // 首轮直接传整串，避免对 GB 级消息执行 msg.substr(0) 的全量拷贝；仅部分发送时才取剩余
        sentBytes = (totalSent == 0) ? client_->Write(msg) : client_->Write(msg.substr(totalSent));
        // partial send return sent bytes
        if (sentBytes <= 0) {
            return static_cast<ssize_t>(totalSent);
        }
        totalSent += static_cast<size_t>(sentBytes);
    }
    return static_cast<ssize_t>(totalSent);
}

ssize_t LocalProcess::NotifyKernelBinary(std::vector<char> const &elfData) const {
    if (client_ == nullptr) {
        return -1;
    }
    // 包头 + 长度（与字节流协议一致），实际二进制不再经过 socket
    std::string head = Serialize(PacketHead{PacketType::KERNEL_BINARY}, static_cast<uint64_t>(elfData.size()));
    int fd = CreateKernelMemfd("msop_kernel_bin");
    if (fd >= 0) {
        bool ok = WriteAllToFd(fd, elfData.data(), elfData.size());
        if (ok) {
            ssize_t sent = client_->WriteWithFd(head, fd);
            close(fd);
            if (sent > 0) {
                return sent;
            }
        } else {
            close(fd);
        }
    }
    // 回退：fd 路径不可用时仍以字节流发送（内存模式 / 老平台）
    std::string msg = std::move(head);
    msg.reserve(msg.size() + elfData.size());
    msg.append(elfData.data(), elfData.size());
    return Notify(msg);
}

ssize_t LocalProcess::Wait(std::string& msg, uint32_t timeOut) const
{
    std::string recvMsg;
    msg.clear();
    ssize_t len = 0;
    // 当msg.size()大于0时说明开始接收，当当len为RECV_BUFF_SIZE数据可能还未传递结束，额外追加判断一次
    for (uint32_t count = 0; (msg.empty() || len == RECV_BUFF_SIZE) && client_ != nullptr;) {
        // 底层为 SOCKET 实现时，socket read 本身存在超时时间为 1s，因此
        // 此接口中的 timeOut 也就是重试 read 的次数
        len = client_->Read(recvMsg);
        if (len > 0) {
            msg += recvMsg;
            continue;
        }
        // 读取成功不占用超时时间，读取失败则增加超时计数
        if (timeOut > 0) {
            if (count >= timeOut) {
                return static_cast<ssize_t>(msg.size());
            }
            count++;
        }
    }
    return static_cast<ssize_t>(msg.size());
}

int LocalProcess::TerminateWithSignal(int signo) const
{
    return kill(getpid(), signo);
}

ssize_t Notify(std::string message)
{
    return LocalProcess::GetInstance().Notify(message);
}

ssize_t Wait(std::string& msg)
{
    return LocalProcess::GetInstance().Wait(msg);
}

void DebugLog(std::string message)
{
    LocalProcess::GetInstance().Log(LogLevel::DEBUG, message);
}

void InfoLog(std::string message)
{
    LocalProcess::GetInstance().Log(LogLevel::INFO, message);
}

void WarnLog(std::string message)
{
    LocalProcess::GetInstance().Log(LogLevel::WARN, message);
}

void ErrorLog(std::string message)
{
    LocalProcess::GetInstance().Log(LogLevel::ERROR, message);
}
