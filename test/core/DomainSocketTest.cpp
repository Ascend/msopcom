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


#define private public
#define protected public
#include "core/DomainSocket.h"
#undef protected
#undef private

#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <memory>
#include "mockcpp/mockcpp.hpp"

void AssertReadMessage(DomainSocketServer &server, std::size_t clientId, std::string const &message)
{
    constexpr std::size_t maxSize = 1024ULL;
    size_t readBytes = 0;
    std::string recieved;
    std::string buffer;
    while (recieved.size() < message.size()) {
        if (server.Read(clientId, buffer, maxSize, readBytes)) {
            recieved += buffer;
        }
    }
    ASSERT_EQ(recieved, message);
}

void AssertWriteMessage(DomainSocketServer &server, std::size_t clientId, std::string const &message)
{
    size_t sentBytes = 0;
    std::size_t totalSent = 0;
    while (totalSent < message.size()) {
        if (server.Write(clientId, message, sentBytes) && sentBytes > 0) {
            totalSent += static_cast<std::size_t>(sentBytes);
        }
    }
    ASSERT_EQ(totalSent, message.size());
}

void AssertReadMessage(DomainSocketClient &client, std::string const &message)
{
    constexpr std::size_t maxSize = 1024ULL;
    size_t readBytes = 0;
    std::string recieved;
    std::string buffer;
    while (recieved.size() < message.size()) {
        if (client.Read(buffer, maxSize, readBytes)) {
            recieved += buffer;
        }
    }
    ASSERT_EQ(recieved, message);
}

void AssertWriteMessage(DomainSocketClient &client, std::string const &message)
{
    size_t writeBytes = 0;
    ASSERT_TRUE(client.Write(message, writeBytes));
    ASSERT_EQ(static_cast<std::size_t>(writeBytes), message.size());
}

void TryConnect(DomainSocketClient &client)
{
    while (!client.Connect()) {
        // server 启动前 client 会连接失败，等待 100ms 后重试
        constexpr uint64_t connectRetryDuration = 100;
        std::this_thread::sleep_for(std::chrono::milliseconds(connectRetryDuration));
    }
}

// 每个用例使用独立的 socket 名：DomainSocket 用的是 Linux abstract socket（sun_path + 1），
// 析构里的 unlink 对其无效，名字只会在所有持有者关闭后释放；一旦有残留的 fork 子进程占名，
// 后续 bind 就会 "Address already in use"（用例偶发失败）甚至串台到旧的子进程（挂死）。
// 这里用 "pid + 递增序号" 保证每个用例名唯一，且与其它进程残留的名字不冲突。
std::string MakeSocketPath()
{
    static size_t seq = 0;
    return "/tmp/msop_connect." + std::to_string(getpid()) + "." + std::to_string(seq++) + ".sock";
}

TEST(DomainSocket, socket_call_failed_expect_create_socket_return_false)
{
    MOCKER(&socket).stubs().will(returnValue(-1));
    std::string socketPath = MakeSocketPath();
    DomainSocket socket(socketPath);
    ASSERT_FALSE(socket.CreateSocket());
    GlobalMockObject::verify();
}

TEST(DomainSocket, set_socket_options_failed_expect_create_socket_return_false)
{
    MOCKER(&setsockopt).stubs().will(returnValue(-1));
    std::string socketPath = MakeSocketPath();
    DomainSocket socket(socketPath);
    ASSERT_FALSE(socket.CreateSocket());
    GlobalMockObject::verify();
}

TEST(DomainSocket, create_socket_failed_expect_listen_and_bind_return_false)
{
    MOCKER(&socket).stubs().will(returnValue(-1));
    std::string socketPath = MakeSocketPath();
    DomainSocketServer socket(socketPath, 1);
    ASSERT_FALSE(socket.ListenAndBind());
    GlobalMockObject::verify();
}

TEST(DomainSocket, bind_failed_expect_listen_and_bind_return_false)
{
    MOCKER(&bind).stubs().will(returnValue(-1));
    std::string socketPath = MakeSocketPath();
    DomainSocketServer socket(socketPath, 1);
    ASSERT_FALSE(socket.ListenAndBind());
    GlobalMockObject::verify();
}

TEST(DomainSocket, listen_failed_expect_listen_and_bind_return_false)
{
    MOCKER(&listen).stubs().will(returnValue(-1));
    std::string socketPath = MakeSocketPath();
    DomainSocketServer socket(socketPath, 1);
    ASSERT_FALSE(socket.ListenAndBind());
    GlobalMockObject::verify();
}

TEST(DomainSocket, accept_failed_expect_accept_return_false)
{
    MOCKER(&accept).stubs().will(returnValue(-1));
    std::string socketPath = MakeSocketPath();
    DomainSocketServer socket(socketPath, 1);
    DomainSocketServer::ClientId clientId;
    ASSERT_FALSE(socket.Accept(clientId));
    GlobalMockObject::verify();
}

TEST(DomainSocket, set_sock_opt_failed_expect_accept_return_false)
{
    MOCKER(&accept).stubs().will(returnValue(1000));
    MOCKER(&setsockopt).stubs().will(returnValue(-1));

    std::string socketPath = MakeSocketPath();
    DomainSocketServer socket(socketPath, 1);
    DomainSocketServer::ClientId clientId;
    ASSERT_FALSE(socket.Accept(clientId));
    GlobalMockObject::verify();
}

TEST(DomainSocket, accept_exceed_max_client_num_expect_accept_return_false)
{
    uid_t uid{0};
    uid_t gid{0};
    MOCKER(&accept).stubs().will(returnValue(1000));
    MOCKER(&setsockopt).stubs().will(returnValue(0));
    MOCKER(&getsockopt).stubs().will(returnValue(0));
    MOCKER(&getuid).stubs().will(returnValue(uid));
    MOCKER(&getgid).stubs().will(returnValue(gid));

    std::string socketPath = MakeSocketPath();
    DomainSocketServer socket(socketPath, 1);
    DomainSocketServer::ClientId clientId;
    ASSERT_TRUE(socket.Accept(clientId));
    ASSERT_FALSE(socket.Accept(clientId));
    GlobalMockObject::verify();
}

TEST(DomainSocket, create_socket_failed_expect_connect_return_false)
{
    MOCKER(&socket).stubs().will(returnValue(-1));
    std::string socketPath = MakeSocketPath();
    DomainSocketClient socket(socketPath);
    ASSERT_FALSE(socket.Connect());
    GlobalMockObject::verify();
}

TEST(DomainSocket, connect_failed_expect_connect_return_false)
{
    MOCKER(&connect).stubs().will(returnValue(-1));
    std::string socketPath = MakeSocketPath();
    DomainSocketClient socket(socketPath);
    ASSERT_FALSE(socket.Connect());
    GlobalMockObject::verify();
}

TEST(DomainSocket, connect_success_expect_connect_return_true)
{
    MOCKER(&connect).stubs().will(returnValue(0));
    std::string socketPath = MakeSocketPath();
    DomainSocketClient socket(socketPath);
    ASSERT_TRUE(socket.Connect());
    GlobalMockObject::verify();
}

TEST(DomainSocket, send_message_from_client_to_server_expect_success)
{
    std::string message = "hello world";
    // fork 前算好名字，父子进程共用同一个 socket 名
    std::string socketPath = MakeSocketPath();

    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        std::shared_ptr<void> defer(nullptr, [](std::nullptr_t&) {_exit(0);});
        // client for child process
        DomainSocketClient socket(socketPath);
        TryConnect(socket);
        AssertWriteMessage(socket, message);
    } else {
        // server for parent process
        DomainSocketServer socket(socketPath, 1);
        typename DomainSocketServer::ClientId id;
        ASSERT_TRUE(socket.ListenAndBind());
        ASSERT_TRUE(socket.Accept(id));
        ASSERT_EQ(id, 0ULL);
        AssertReadMessage(socket, 0, message);
    }
}

TEST(DomainSocket, send_message_from_server_to_client_expect_success)
{
    std::string message = "hello world";
    // fork 前算好名字，父子进程共用同一个 socket 名
    std::string socketPath = MakeSocketPath();

    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        std::shared_ptr<void> defer(nullptr, [](std::nullptr_t&) {_exit(0);});
        // client for child process
        DomainSocketClient socket(socketPath);
        TryConnect(socket);
        AssertReadMessage(socket, message);
    } else {
        // server for parent process
        DomainSocketServer socket(socketPath, 1);
        typename DomainSocketServer::ClientId id;
        ASSERT_TRUE(socket.ListenAndBind());
        ASSERT_TRUE(socket.Accept(id));
        ASSERT_EQ(id, 0ULL);
        AssertWriteMessage(socket, 0, message);
    }
}

TEST(DomainSocket, send_interleaving_message_expect_success)
{
    std::vector<std::string> messages = {"first", "second", "third", "forth"};
    // fork 前算好名字，父子进程共用同一个 socket 名
    std::string socketPath = MakeSocketPath();

    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        // client for child process
        DomainSocketClient socket(socketPath);
        TryConnect(socket);
        AssertReadMessage(socket, messages[0] + messages[1]);
        AssertWriteMessage(socket, messages[2]);
        AssertReadMessage(socket, messages[3]);
        _exit(0);
    } else {
        // server for parent process
        DomainSocketServer socket(socketPath, 1);
        typename DomainSocketServer::ClientId id;
        ASSERT_TRUE(socket.ListenAndBind());
        ASSERT_TRUE(socket.Accept(id));
        ASSERT_EQ(id, 0ULL);
        AssertWriteMessage(socket, 0, messages[0]);
        AssertWriteMessage(socket, 0, messages[1]);
        AssertReadMessage(socket, 0, messages[2]);
        AssertWriteMessage(socket, 0, messages[3]);
    }
}

TEST(DomainSocket, send_messages_with_client_in_parent_and_server_in_child_expect_success)
{
    std::vector<std::string> messages = {"first", "second", "third", "forth"};
    // fork 前算好名字，父子进程共用同一个 socket 名
    std::string socketPath = MakeSocketPath();

    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        std::shared_ptr<void> defer(nullptr, [](std::nullptr_t&) {_exit(0);});
        // server for child process
        DomainSocketServer socket(socketPath, 1);
        typename DomainSocketServer::ClientId id;
        ASSERT_TRUE(socket.ListenAndBind());
        ASSERT_TRUE(socket.Accept(id));
        ASSERT_EQ(id, 0ULL);
        AssertWriteMessage(socket, 0, messages[0]);
        AssertWriteMessage(socket, 0, messages[1]);
        AssertReadMessage(socket, 0, messages[2]);
        AssertWriteMessage(socket, 0, messages[3]);
    } else {
        // client for parent process
        DomainSocketClient socket(socketPath);
        TryConnect(socket);
        AssertReadMessage(socket, messages[0] + messages[1]);
        AssertWriteMessage(socket, messages[2]);
        AssertReadMessage(socket, messages[3]);
    }
}

TEST(DomainSocket, multiple_clients_connect_to_server_and_send_messages_expect_success)
{
    std::vector<std::string> messages = {"first", "second", "third", "forth"};
    // fork 前算好名字，父子进程共用同一个 socket 名
    std::string socketPath = MakeSocketPath();

    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        std::shared_ptr<void> defer(nullptr, [](std::nullptr_t&) {_exit(0);});
        // client for child process
        DomainSocketClient socket0(socketPath);
        DomainSocketClient socket1(socketPath);
        DomainSocketClient socket2(socketPath);
        DomainSocketClient socket3(socketPath);
        TryConnect(socket0);
        TryConnect(socket1);
        TryConnect(socket2);
        TryConnect(socket3);
        AssertReadMessage(socket0, messages[0]);
        AssertReadMessage(socket1, messages[1]);
        AssertWriteMessage(socket2, messages[2]);
        AssertReadMessage(socket3, messages[3]);
    } else {
        // server for parent process
        DomainSocketServer socket(socketPath, 4);
        typename DomainSocketServer::ClientId id;
        ASSERT_TRUE(socket.ListenAndBind());
        for (std::size_t clientNum = 0; clientNum < 4; ++clientNum) {
            ASSERT_TRUE(socket.Accept(id));
            ASSERT_EQ(id, clientNum);
        }
        ASSERT_EQ(socket.GetClientNum(), 4ULL);
        AssertWriteMessage(socket, 0, messages[0]);
        AssertWriteMessage(socket, 1, messages[1]);
        AssertReadMessage(socket, 2, messages[2]);
        AssertWriteMessage(socket, 3, messages[3]);
    }
}
