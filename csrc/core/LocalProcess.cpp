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


#include "LocalProcess.h"

#include <map>
#include <thread>
#include <chrono>
#include <unistd.h>

#include "FuncSelector.h"
#include "utils/InjectLogger.h"

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
        sentBytes = client_->Write(msg.substr(totalSent));
        // partial send return sent bytes
        if (sentBytes <= 0) {
            return static_cast<ssize_t>(totalSent);
        }
        totalSent += static_cast<size_t>(sentBytes);
    }
    return static_cast<ssize_t>(totalSent);
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
