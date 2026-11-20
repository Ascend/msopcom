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


#include "Communication.h"

#include <queue>
#include <memory>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdlib>

#include "DomainSocket.h"
#include "LocalProcess.h"
#include "utils/Future.h"
#include "utils/InjectLogger.h"
#include "utils/FileSystem.h"
#include "utils/Ustring.h"

namespace {
constexpr char const *MSOP_SOCKET_PATH_ENV = "MSOP_SOCKET_PATH";
// LocalComm类提供本地通信能力，用于调试使用
class LocalComm {
public:
    ~LocalComm() = default;

    void Clear()
    {
        while (!s2c.empty()) {
            s2c.pop();
        }
        while (!c2s.empty()) {
            c2s.pop();
        }
    }

    static LocalComm* Instance()
    {
        static LocalComm instance;
        return &instance;
    }

    ssize_t Read(const std::string& flag, std::string& msg)
    {
        if (flag == "s" && !c2s.empty()) {
            msg = c2s.front();
            c2s.pop();
            return static_cast<ssize_t>(msg.size());
        }
        if (flag == "c" && !s2c.empty()) {
            msg = s2c.front();
            s2c.pop();
            return static_cast<ssize_t>(msg.size());
        }
        return 0;
    }

    ssize_t Write(const std::string& flag, std::string const& msg)
    {
        if (flag == "s") {
            s2c.push(msg);
            return static_cast<ssize_t>(msg.size());
        }
        if (flag == "c") {
            c2s.push(msg);
            return static_cast<ssize_t>(msg.size());
        }
        return 0;
    }

private:
    explicit LocalComm() {}

private:
    std::queue<std::string> s2c;
    std::queue<std::string> c2s;
};

class SocketServer : public CommunicationServer {
public:
    explicit SocketServer(std::size_t maxClientNum);
    ~SocketServer() override;
    bool Start() override;
    ssize_t Read(ClientId clientId, std::string &msg) override;
    ssize_t Write(ClientId clientId, std::string const& msg) override;
    void SetClientConnectHook(ClientConnectHook &&hook) override;
    void SetMsgHandlerHook(ClientMsgHandlerHook &&hook) override;

protected:
    std::unique_ptr<DomainSocketServer> socket_;
    std::size_t maxClientNum_;
    std::thread acceptWorker_;
    ClientConnectHook clientConnectHook_;
    ClientMsgHandlerHook clientMsgHandlerHook_;

private:
    void Listen(ClientId clientId);

    std::vector<std::thread> readWorker_;
    std::atomic<bool> isRunning_;
};

SocketServer::SocketServer(std::size_t maxClientNum)
    : maxClientNum_(maxClientNum), isRunning_(true)
{
    auto pid = getpid();
    std::string timeStamp = GenerateTimeStamp<std::chrono::nanoseconds>();
    std::string socketPath = "/tmp/msop_connect." + timeStamp + "." + std::to_string(pid) + ".sock";
    ::setenv(MSOP_SOCKET_PATH_ENV, socketPath.c_str(), 1);
    socket_ = MakeUnique<DomainSocketServer>(socketPath, maxClientNum);
}

SocketServer::~SocketServer()
{
    isRunning_ = false;
    if (acceptWorker_.joinable()) {
        acceptWorker_.join();
    }
    for (auto &th : readWorker_) {
        if (th.joinable()) {
            th.join();
        }
    }
}

bool SocketServer::Start()
{
    if (!socket_->ListenAndBind()) {
        socket_ = nullptr;
        return false;
    }

    // 在子线程中处理多客户端连接
    acceptWorker_ = std::thread([this]() {
        while (isRunning_ && socket_->GetClientNum() < maxClientNum_) {
            ClientId clientId;
            if (!socket_->Accept(clientId)) {
                // Accept 的超时时间与 Read 相同，都是 1000ms
                continue;
            }
            if (clientMsgHandlerHook_ != nullptr) {
                std::thread th = std::thread(&SocketServer::Listen, this, clientId);
                readWorker_.emplace_back(std::move(th));
            }
            // 通知回调函数处理客户端连接事件
            if (clientConnectHook_) {
                clientConnectHook_(clientId);
            }
        }
    });
    return true;
}
void SocketServer::Listen(ClientId clientId)
{
    std::string msg;
    while (isRunning_) {
        int len = Read(clientId, msg);
        if (len <= 0) {
            continue;
        }
        clientMsgHandlerHook_(clientId, msg);
    }
}

ssize_t SocketServer::Read(ClientId clientId, std::string &msg)
{
    if (socket_ == nullptr) {
        return -1;
    }

    constexpr std::size_t maxSize = 1024ULL;
    size_t readSize = 0;
    if (!socket_->Read(clientId, msg, maxSize, readSize)) {
        return -1;
    }
    return static_cast<ssize_t>(readSize);
}

ssize_t SocketServer::Write(ClientId clientId, std::string const &msg)
{
    if (socket_ == nullptr) {
        return -1;
    }

    size_t sendBytes = 0;
    return socket_->Write(clientId, msg, sendBytes) ? static_cast<ssize_t>(sendBytes) : -1;
}

void SocketServer::SetClientConnectHook(ClientConnectHook &&hook)
{
    clientConnectHook_ = hook;
}

void SocketServer::SetMsgHandlerHook(ClientMsgHandlerHook &&hook)
{
    clientMsgHandlerHook_ = hook;
}

class SocketClient : public CommunicationClient {
public:
    SocketClient();
    ~SocketClient() override = default;
    bool Connect() override;
    ssize_t Read(std::string &msg) override;
    ssize_t Write(std::string const& msg) override;
    ssize_t WriteWithFd(std::string const &msg, int fd) override;

protected:
    std::unique_ptr<DomainSocketClient> socket_;
};

SocketClient::SocketClient()
{
    const char *sockPathchar = secure_getenv(MSOP_SOCKET_PATH_ENV);
    std::string socketPath = (sockPathchar == nullptr) ? "" : sockPathchar;
    if (socketPath.empty()) {
        WARN_LOG("Socket client get env failed");
        socket_ = nullptr;
    } else {
        socket_ = MakeUnique<DomainSocketClient>(socketPath);
    }
}

bool SocketClient::Connect()
{
    if (socket_ == nullptr) {
        return false;
    }

    return socket_->Connect();
}

ssize_t SocketClient::Read(std::string &msg)
{
    if (socket_ == nullptr) {
        return -1;
    }

    size_t readSize = 0;
    if (!socket_->Read(msg, RECV_BUFF_SIZE, readSize)) {
        return -1;
    }
    return static_cast<ssize_t>(readSize);
}

ssize_t SocketClient::Write(const std::string &msg)
{
    if (socket_ == nullptr) {
        return -1;
    }

    size_t sendBytes = 0;
    return socket_->Write(msg, sendBytes) ? static_cast<ssize_t>(sendBytes) : -1;
}

ssize_t SocketClient::WriteWithFd(const std::string &msg, int fd) {
    if (socket_ == nullptr) {
        return -1;
    }

    size_t sendBytes = 0;
    return socket_->WriteWithFd(msg, fd, sendBytes) ? static_cast<ssize_t>(sendBytes) : -1;
}

} // namespace

Server::Server(CommType type) : type_(type)
{
    if (type == CommType::SOCKET) {
        // 目前先设置最大客户端连接数为 256，后续可以考虑作为参数从外部传入
        constexpr std::size_t maxClientNum = 256;
        socketServer_ = MakeUnique<SocketServer>(maxClientNum);
    }
}

Server::~Server()
{
    LocalComm::Instance()->Clear();
}

bool Server::Start()
{
    if (type_ == CommType::MEMORY) {
        return true;
    } else if (type_ == CommType::SOCKET) {
        return socketServer_ != nullptr && socketServer_->Start();
    }
    return false;
}

ssize_t Server::Read(ClientId clientId, std::string &msg)
{
    ssize_t len = 0;
    if (type_ == CommType::MEMORY) {
        len = LocalComm::Instance()->Read("s", msg);
    } else if (type_ == CommType::SOCKET) {
        len = socketServer_->Read(clientId, msg);
    }
    return len;
}

ssize_t Server::Write(ClientId clientId, std::string const &msg)
{
    ssize_t len = 0;
    if (type_ == CommType::MEMORY) {
        len = LocalComm::Instance()->Write("s", msg);
    } else if (type_ == CommType::SOCKET) {
        len = socketServer_->Write(clientId, msg);
    }
    return len;
}

void Server::SetClientConnectHook(ClientConnectHook &&hook)
{
    if (socketServer_ != nullptr) {
        socketServer_->SetClientConnectHook(std::move(hook));
    }
}

void Server::SetMsgHandlerHook(ClientMsgHandlerHook &&hook)
{
    if (socketServer_ != nullptr) {
        socketServer_->SetMsgHandlerHook(std::move(hook));
    }
}

Client::Client(CommType type) : type_(type)
{
    if (type == CommType::SOCKET) {
        socketClient_ = MakeUnique<SocketClient>();
    }
}

Client::~Client()
{
    LocalComm::Instance()->Clear();
}

bool Client::Connect()
{
    if (type_ == CommType::MEMORY) {
        return true;
    } else if (type_ == CommType::SOCKET) {
        return socketClient_->Connect();
    }
    return false;
}

ssize_t Client::Read(std::string &msg)
{
    ssize_t len = 0;
    if (type_ == CommType::MEMORY) {
        len = LocalComm::Instance()->Read("c", msg);
    } else if (type_ == CommType::SOCKET) {
        len = socketClient_->Read(msg);
    }
    return len;
}

ssize_t Client::Write(std::string const &msg)
{
    ssize_t len = 0;
    if (type_ == CommType::MEMORY) {
        len = LocalComm::Instance()->Write("c", msg);
    } else if (type_ == CommType::SOCKET) {
        len = socketClient_->Write(msg);
    }
    return len;
}

ssize_t Client::WriteWithFd(std::string const &msg, int fd) {
    if (type_ == CommType::SOCKET && socketClient_ != nullptr) {
        return socketClient_->WriteWithFd(msg, fd);
    }
    return -1; // 内存模式不支持 fd，调用方回退到字节流
}
