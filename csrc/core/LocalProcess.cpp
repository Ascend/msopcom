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
#include <cstdlib>
#include <limits>
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
#include "runtime/inject_helpers/LocalDevice.h"
#include "utils/InjectLogger.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"

// memfd_create 包装函数依赖较新的 glibc（>=2.27）；这里直接走 syscall，
// 兼容较老的构建镜像。内核不支持时返回 -1，由调用方回退到字节流发送。
int LocalProcess::CreateKernelMemfd(const char *name) {
#if defined(SYS_memfd_create)
    return static_cast<int>(syscall(SYS_memfd_create, name, MFD_CLOEXEC));
#else
    (void)name;
    return -1;
#endif
}

namespace {
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

LocalProcess::ProcessState &LocalProcess::State() {
    // HCCL 的业务全局对象可能在工具静态析构之后释放设备内存，连接和编号须存活至进程退出，改成 new 后交给操作系统回收
    static ProcessState *state = new ProcessState;
    return *state;
}

void LocalProcess::CheckProcessOwner() {
    if (State().ownerPid != getpid()) {
        // fork 子进程可能继承了日志锁；直接写标准错误，避免退出前卡住。
        constexpr char message[] = "mssanitizer: forked child cannot reuse device connections inherited from "
                                   "the parent process; use exec to start a new process.\n";
        const ssize_t written = write(STDERR_FILENO, message, sizeof(message) - 1);
        (void)written;
        _exit(EXIT_FAILURE);
    }
}

// 为设备业务包原子保留进程级编号；fork 后拒绝复用父进程的编号源。
uint64_t LocalProcess::ReserveOrderId() {
    auto &state = State();
    CheckProcessOwner();
    uint64_t next = state.nextOrderId.load(std::memory_order_relaxed);
    while (next != 0 && next != std::numeric_limits<uint64_t>::max()) {
        if (state.nextOrderId.compare_exchange_weak(next, next + 1, std::memory_order_relaxed)) {
            return next;
        }
    }
    return 0;
}

LocalProcess::LocalProcess(CommType type, bool isDeviceConnection) : isDeviceConnection_(isDeviceConnection) {
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

// 通用通知沿用字节发送，设备业务包在发送前统一填入进程编号
ssize_t LocalProcess::Notify(std::string msg) const {
    if (!isDeviceConnection_) {
        return SendBytes(msg);
    }
    CheckProcessOwner();
    if (msg.size() < sizeof(PacketHead)) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(sendMutex_);
    PacketHead head{};
    std::memcpy(&head, msg.data(), sizeof(head));
    head.orderId = ReserveOrderId();
    if (head.orderId == 0) {
        return -1;
    }
    // 持有消息本身并原地填号，临时消息无需再复制整包
    std::memcpy(&msg[0], &head, sizeof(head));
    const ssize_t sent = SendBytes(msg);
    if (sent != static_cast<ssize_t>(msg.size())) {
        ERROR_LOG("Device packet send failed; detection result is incomplete (orderId=%lu).", head.orderId);
    }
    return sent;
}

ssize_t LocalProcess::SendBytes(std::string const &msg) const {
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
    // 设备连接先检查 PID 再加锁，避免 fork 子进程等待父进程遗留的锁
    std::unique_lock<std::mutex> lock(sendMutex_, std::defer_lock);
    uint64_t orderId = 0;
    if (isDeviceConnection_) {
        CheckProcessOwner();
        lock.lock();
        orderId = ReserveOrderId();
        if (orderId == 0) {
            return -1;
        }
    }
    if (client_ == nullptr) {
        ERROR_LOG("Kernel binary send failed; detection result is incomplete (orderId=%lu).", orderId);
        return -1;
    }
    // 包头 + 长度（与字节流协议一致），实际二进制不再经过 socket
    std::string head =
        Serialize(PacketHead{PacketType::KERNEL_BINARY, 0, orderId}, static_cast<uint64_t>(elfData.size()));
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
    // 回退沿用已有包头和编号，直接发送字节，避免重复取号或重复加锁
    const ssize_t sent = SendBytes(msg);
    if (sent <= 0) {
        ERROR_LOG("Kernel binary send failed; detection result is incomplete (orderId=%lu).", orderId);
    }
    return sent;
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

ssize_t Notify(std::string message) { return LocalProcess::GetInstance().Notify(std::move(message)); }

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
