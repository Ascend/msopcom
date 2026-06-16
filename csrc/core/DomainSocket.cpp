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

#include "DomainSocket.h"
#include <vector>
#include <string>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "utils/InjectLogger.h"

constexpr size_t MAX_PRINT_BYTES = 1024;

DomainSocket::DomainSocket(const std::string &socketPath) : socketPath_(socketPath) { }

DomainSocket::~DomainSocket()
{
    if (sfd_ != -1) {
        close(sfd_);
    }
}

bool DomainSocket::CreateSocket()
{
    sfd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd_ == -1) {
        WARN_LOG("Socket create failed");
        return false;
    }

    addr_ = sockaddr_un{};
    addr_.sun_family = AF_UNIX;
    size_t minSunPathLeft = 2;
    socketPath_.copy(addr_.sun_path + 1, std::min(sizeof(addr_.sun_path) - minSunPathLeft, socketPath_.size()));

    auto timeout = timeval {};
    timeout.tv_sec = 1; // 暂设置read超时时间为1s
    timeout.tv_usec = 0;
    if (setsockopt(sfd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
        WARN_LOG("Socket set timeout failed: %s", std::string(strerror(errno)).c_str());
        return false;
    }
    int opt = 1;
    if (setsockopt(sfd_, SOL_SOCKET, SO_PASSCRED, &opt, sizeof(opt)) == -1) {
        WARN_LOG("Socket set SO_PEERCRED failed: %s", std::string(strerror(errno)).c_str());
        return false;
    }

    return true;
}

DomainSocketServer::DomainSocketServer(const std::string &socketPath, std::size_t maxClientNum)
    : DomainSocket(socketPath), maxClientNum_(maxClientNum) {}

DomainSocketServer::~DomainSocketServer()
{
    for (int32_t fd : cfds_) {
        close(fd);
    }
    // 由服务端关闭文件节点
    unlink(addr_.sun_path);
}

bool DomainSocketServer::ListenAndBind()
{
    // for server: socket() -> bind() -> listen -> accept() -> read/write()
    if (!CreateSocket()) {
        return false;
    }

    if (bind(sfd_, reinterpret_cast<sockaddr *>(&addr_), sizeof(struct sockaddr_un)) == -1) {
        WARN_LOG("Server bind socket failed: %s", std::string(strerror(errno)).c_str());
        return false;
    }

    if (listen(sfd_, static_cast<int>(maxClientNum_)) == -1) { // 仅支持最多1个client
        WARN_LOG("Server listen failed");
        return false;
    }

    return true;
}

bool DomainSocketServer::Accept(ClientId &id)
{
    if (cfds_.size() >= maxClientNum_) {
        WARN_LOG("Server accept failed, exceed max client number");
        return false;
    }

    int32_t cfd = accept(sfd_, nullptr, nullptr);
    if (cfd == -1) {
        return false;
    }
    struct timeval timeout{};
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    // 设置接收超时时间
    if (setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
        close(cfd);
        WARN_LOG("Client fd set timeout failed");
        return false;
    }
    // 获取客户端凭证（uid/gid）
    struct ucred cred{};
    socklen_t cred_len = sizeof(cred);
    if (getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) == -1) {
        WARN_LOG("get client SO_PEERCRED failed: %s", std::string(strerror(errno)).c_str());
        return false;
    }
    if (getuid() != cred.uid || getgid() != cred.gid) {
        WARN_LOG("client SO_PEERCRED check permission failed, recv id: uid=%d, gid=%d", cred.uid, cred.gid);
        close(cfd);
        return false;
    }

    // save valid cfd handle to cfds_
    id = cfds_.size();
    {
        // lock client sequence as critical zone
        std::lock_guard<std::mutex> guard(cfdsMutex_);
        cfds_.push_back(cfd);
    }
    return true;
}

std::size_t DomainSocketServer::GetClientNum() const
{
    return cfds_.size();
}

static void PrintCommInfo(const std::string &message, size_t commSize, bool isRead, int sfd, bool isServer)
{
    static std::string readName{"read"};
    static std::string writeName{"write"};
    static std::mutex mux;
    {
        std::lock_guard<std::mutex> locker(mux);
        VERBOSE_LOG("==== %s %s size=%zu senderFd=%d ====",
            isServer ? "DomainSocketServer" : "DomainSocketClient",
            isRead ? readName.c_str() : writeName.c_str(),
            commSize,
            sfd);
        int32_t printBytes = std::min(MAX_PRINT_BYTES, commSize);
        std::stringstream ss;
        for (int i = 0; i < printBytes; i++) {
            ss << std::hex << static_cast<int32_t>(message[i]) << " ";
        }
        VERBOSE_LOG("%s", ss.str().c_str());
        VERBOSE_LOG("==== %s %s done ====",
                    isServer ? "DomainSocketServer" : "DomainSocketClient",
                    isRead ? readName.c_str() : writeName.c_str());
        fflush(stdout);
    }
}

bool DomainSocketServer::Read(ClientId id, std::string &message, size_t maxBytes, size_t &receivedBytes)
{
    int32_t cfd;
    {
        // lock client sequence as critical zone
        std::lock_guard<std::mutex> guard(cfdsMutex_);
        if (id >= cfds_.size() || cfds_[id] == -1) {
            return false;
        }
        cfd = cfds_[id];
    }

    std::vector<char> buffer(maxBytes);
    ssize_t ret = read(cfd, buffer.data(), maxBytes);
    if (ret == -1) {
        return false;
    }

    receivedBytes = static_cast<size_t>(ret);
    if (ret > 0) {
        message.assign(buffer.data(), receivedBytes);
        PrintCommInfo(message, receivedBytes, true, cfd, false);
    }

    return true;
}

bool DomainSocketServer::Write(ClientId id, const std::string &message, size_t &sentBytes)
{
    int32_t cfd;
    {
        // lock client sequence as critical zone
        std::lock_guard<std::mutex> guard(cfdsMutex_);
        if (id >= cfds_.size() || cfds_[id] == -1) {
            return false;
        }
        cfd = cfds_[id];
    }

    auto buffer = message.data();
    auto size = message.size();
    ssize_t ret;
    sentBytes = 0;

    if (size > 0) {
        PrintCommInfo(message, size, false, cfd, true);
    }

    while (size > 0) {
        ret = write(cfd, buffer, size);
        if (ret == -1) {
            // 如果 write 返回 -1 说明对端已关闭，返回已写入字符数
            return false;
        }
        // 发送成功，ret为已发送字节数
        size_t writeBytes = static_cast<size_t>(ret);
        sentBytes += writeBytes;
        size -= writeBytes;
        buffer += writeBytes;
    }

    return true;
}

DomainSocketClient::DomainSocketClient(const std::string &socketPath) : DomainSocket(socketPath) {}

DomainSocketClient::~DomainSocketClient() { }

bool DomainSocketClient::Connect()
{
    // for client: socket() -> connect() -> read/write()
    if (!CreateSocket()) {
        return false;
    }

    // client uses socket sfd_ for read/write operation
    if (connect(sfd_, reinterpret_cast<sockaddr *>(&addr_), sizeof(addr_)) == -1) {
        return false;
    }

    return true;
}

bool DomainSocketClient::Read(std::string &message, size_t maxBytes, size_t &receivedBytes) const
{
    if (sfd_ == -1) {
        return false;
    }

    std::vector<char> buffer(maxBytes);
    ssize_t ret = read(sfd_, buffer.data(), maxBytes);
    if (ret == -1) {
        return false;
    }

    receivedBytes = static_cast<size_t>(ret);
    if (ret > 0) {
        message.assign(buffer.data(), receivedBytes);
        PrintCommInfo(message, receivedBytes, true, this->sfd_, false);
    }

    return true;
}

bool DomainSocketClient::Write(const std::string &message, size_t &sentBytes)
{
    if (sfd_ == -1) {
        return false;
    }

    auto buffer = message.data();
    auto size = message.size();
    ssize_t ret;
    sentBytes = 0;

    if (size > 0) {
        PrintCommInfo(message, size, false, this->sfd_, false);
    }

    while (size > 0) {
        {
            std::lock_guard<std::mutex> lockGuard(mux_);
            ret = static_cast<int32_t>(write(sfd_, buffer, size));
        }
        if (ret == -1) {
            // 如果write返回-1，则直接返回，sentBytes返回已发送字节数
            return false;
        }
        // 发送成功，ret为已发送字节数
        size_t writeBytes = static_cast<size_t>(ret);
        sentBytes += writeBytes;
        size -= writeBytes;
        buffer += writeBytes;
    }

    return true;
}
