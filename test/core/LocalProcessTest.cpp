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


#include <thread>
#include <limits>
#include <fcntl.h>
#include "utils/Serialize.h"
#include "utils/Protocol.h"
#include <gtest/gtest.h>
#define private public
#define protected public
#include "core/LocalProcess.h"
#undef private
#undef protected
#include "core/RemoteProcess.h"
#include "mockcpp/mockcpp.hpp"

// 针对多进程间的信息交互，在UT中采用多线程进行模拟
// 针对用例的设计，需要覆盖基础的接口
// 说明：对于阻塞接口，需要多线程实现；其他可以单独实现

TEST(LocalProcess, send_log_expect_success)
{
    RemoteProcess remote(CommType::MEMORY);
    LocalProcess::GetInstance(CommType::MEMORY).Log(LogLevel::ERROR, "something happens.");
    std::string recvMsg;
    (void)remote.Wait(0, recvMsg);
    ASSERT_STREQ(recvMsg.c_str(), "[LOG][ERROR] something happens.;");
}

TEST(LocalProcess, notify_no_block_expect_success)
{
    RemoteProcess remote(CommType::MEMORY);
    LocalProcess::GetInstance(CommType::MEMORY).Notify("test");
    std::string recvMsg;
    (void)remote.Wait(0, recvMsg);
    std::string notifyMsg = "test";
    ASSERT_STREQ(recvMsg.c_str(), notifyMsg.c_str());
}

TEST(LocalProcess, wait_expect_success)
{
    RemoteProcess remote(CommType::MEMORY);
    std::string sendMsg = "hello";
    remote.Notify(0, sendMsg);
    std::string recvMsg = "";
    LocalProcess::GetInstance(CommType::MEMORY).Wait(recvMsg);
    ASSERT_STREQ(recvMsg.c_str(), sendMsg.c_str());
}

TEST(LocalProcess, wait_api_test_return_success)
{
    MOCKER(&LocalProcess::Wait).stubs().will(returnValue(ssize_t(0)));
    std::string msg;
    ASSERT_EQ(Wait(msg), 0);
    GlobalMockObject::verify();
}

TEST(LocalProcess, log_api_test)
{
    MOCKER(&LocalProcess::Log).stubs().will(returnValue(0));
    std::string log = "test";
    DebugLog(log);
    InfoLog(log);
    WarnLog(log);
}

namespace {
// 用Client原有虚接口替换发送端，不修改产品接口，也不对虚函数地址打桩。
class FragmentingClient : public Client {
public:
    FragmentingClient() : Client(CommType::MEMORY) {}
    ssize_t WriteWithFd(const std::string &message, int fd) override {
        fdHeader = message;
        capturedFd = fd;
        return fdResult;
    }
    ssize_t Write(const std::string &message) override {
        const size_t count = std::min(size_t{3}, message.size());
        written.append(message.data(), count);
        return static_cast<ssize_t>(count);
    }
    ssize_t fdResult{-1};
    int capturedFd{-1};
    std::string written;
    std::string fdHeader;
};
}

TEST(LocalProcess, binary_fd_failure_and_success_keep_one_packet) {
    LocalProcess process(CommType::MEMORY, true);
    auto *client = new FragmentingClient();
    delete process.client_;
    process.client_ = client;
    const std::vector<char> binary{'p', 'e', 'e', 'r'};
    uint64_t previousOrder = 0;
    for (ssize_t sent : {ssize_t{-1}, static_cast<ssize_t>(sizeof(PacketHead) + sizeof(uint64_t))}) {
        SCOPED_TRACE(sent);
        // 用可写 fd 替代 memfd 创建，发送成功与失败分支都不依赖系统的 memfd 支持
        const int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
        ASSERT_GE(fd, 0);
        MOCKER(&LocalProcess::CreateKernelMemfd).stubs().will(returnValue(fd));
        client->written.clear();
        client->fdResult = sent;
        const ssize_t result = process.NotifyKernelBinary(binary);
        GlobalMockObject::verify();
        PacketHead head{};
        ASSERT_TRUE(Deserialize(client->fdHeader.substr(0, sizeof(head)), head));
        EXPECT_GT(head.orderId, 0);
        if (previousOrder != 0) {
            EXPECT_EQ(head.orderId, previousOrder + 1);
        }
        previousOrder = head.orderId;
        const auto header = Serialize(PacketHead{PacketType::KERNEL_BINARY, 0, head.orderId}, uint64_t{binary.size()});
        EXPECT_EQ(client->fdHeader, header);
        EXPECT_EQ(client->capturedFd, fd);
        EXPECT_EQ(fcntl(client->capturedFd, F_GETFD), -1);
        if (sent < 0) {
            const auto expected = header + std::string(binary.begin(), binary.end());
            EXPECT_EQ(result, expected.size());
            EXPECT_EQ(client->written, expected);
        } else {
            EXPECT_EQ(result, header.size());
            EXPECT_TRUE(client->written.empty());
        }
    }
}

// 分别模拟 fd 创建失败和写入失败，独立验证两条准备失败回退路径
TEST(LocalProcess, binary_preparation_failure_falls_back_to_complete_stream) {
    LocalProcess process(CommType::MEMORY, true);
    auto *client = new FragmentingClient();
    delete process.client_;
    process.client_ = client;
    const std::vector<char> binary{'p', 'e', 'e', 'r'};
    uint64_t previousOrder = 0;
    for (bool failCreate : {true, false}) {
        SCOPED_TRACE(failCreate);
        client->written.clear();
        client->capturedFd = -1;
        // 写入失败场景先提供有效 fd，避免环境不支持 memfd 时提前走创建失败分支
        const int fd = failCreate ? -1 : open("/dev/null", O_RDWR | O_CLOEXEC);
        if (!failCreate) {
            ASSERT_GE(fd, 0);
            MOCKER(&write).stubs().will(returnValue(ssize_t{-1}));
        }
        MOCKER(&LocalProcess::CreateKernelMemfd).stubs().will(returnValue(fd));
        const auto result = process.NotifyKernelBinary(binary);
        GlobalMockObject::verify();
        if (fd >= 0) {
            EXPECT_EQ(fcntl(fd, F_GETFD), -1);
        }
        PacketHead head{};
        ASSERT_TRUE(Deserialize(client->written.substr(0, sizeof(head)), head));
        EXPECT_GT(head.orderId, 0);
        if (previousOrder != 0) {
            EXPECT_EQ(head.orderId, previousOrder + 1);
        }
        previousOrder = head.orderId;
        const auto expected =
            Serialize(PacketHead{PacketType::KERNEL_BINARY, 0, head.orderId}, uint64_t{binary.size()}) +
            std::string(binary.begin(), binary.end());
        EXPECT_EQ(result, expected.size());
        EXPECT_EQ(client->written, expected);
        EXPECT_EQ(client->capturedFd, -1); // 准备失败时不能尝试SCM_RIGHTS发送。
    }
}

// 无效包不取号；编号耗尽后拒绝发送，避免编号回绕造成工具侧等待错误的包
TEST(LocalProcess, invalid_packet_and_exhausted_counter_do_not_send) {
    LocalProcess process(CommType::MEMORY, true);
    auto *client = new FragmentingClient();
    delete process.client_;
    process.client_ = client;
    auto &counter = LocalProcess::State().nextOrderId;
    const auto saved = counter.load();
    EXPECT_EQ(process.Notify(""), -1);
    EXPECT_EQ(counter.load(), saved);
    counter.store(std::numeric_limits<uint64_t>::max());
    const auto packet = Serialize(PacketHead{PacketType::DEVICE_SUMMARY, 0, 0});
    const auto normalResult = process.Notify(packet);
    const auto binaryResult = process.NotifyKernelBinary({'b'});
    counter.store(0);
    const auto zeroResult = LocalProcess::ReserveOrderId();
    counter.store(saved); // 恢复进程级计数器，避免影响后续用例
    EXPECT_EQ(normalResult, -1);
    EXPECT_EQ(binaryResult, -1);
    EXPECT_EQ(zeroResult, 0);
    EXPECT_TRUE(client->written.empty());
    EXPECT_EQ(client->capturedFd, -1);
}
