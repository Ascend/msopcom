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
#include <future>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "core/RemoteProcess.h"
#include "mockcpp/mockcpp.hpp"
#define private public
#include "runtime/inject_helpers/LocalDevice.h"
#undef private
#include "utils/Protocol.h"
#include "utils/Serialize.h"

// 针对多进程间的信息交互，在UT中采用多线程进行模拟
// 针对用例的设计，需要覆盖基础的接口
// 说明：对于阻塞接口，需要多线程实现；其他可以单独实现

TEST(LocalDevice, send_log_expect_success)
{
    RemoteProcess remote(CommType::MEMORY);
    int32_t mockDeviceId = 0;
    LocalDevice::GetInstance(mockDeviceId, CommType::MEMORY).Log(LogLevel::ERROR, "something happens.");
    std::string recvMsg;
    (void)remote.Wait(0, recvMsg);
    ASSERT_STREQ(recvMsg.c_str(), "[LOG][ERROR] something happens.;");
}

TEST(LocalDevice, notify_no_block_expect_success)
{
    RemoteProcess remote(CommType::MEMORY);
    int32_t mockDeviceId = 0;
    const std::string message =
        Serialize(PacketHead{PacketType::PEER_ACCESS_RECORD, 0, 0}, PeerAccessRecord{0, 1, PeerAccessOp::ENABLE});
    ASSERT_EQ(
        LocalDevice::GetInstance(mockDeviceId, CommType::MEMORY).Notify(message), static_cast<ssize_t>(message.size()));
    std::string recvMsg;
    (void)remote.Wait(0, recvMsg);
    PacketHead head{};
    ASSERT_TRUE(Deserialize(recvMsg.substr(0, sizeof(head)), head));
    ASSERT_EQ(head.type, PacketType::PEER_ACCESS_RECORD);
    ASSERT_GT(head.orderId, 0);
    EXPECT_EQ(recvMsg.substr(sizeof(head)), message.substr(sizeof(head)));
}

// 内存通信替身为进程共享队列，每例开始前排空其它测试残留
class LocalDeviceQueueTest : public testing::Test {
protected:
    void SetUp() override {
        RemoteProcess remote(CommType::MEMORY);
        std::string stale;
        while (remote.Wait(0, stale) > 0) {
        }
    }
};

TEST_F(LocalDeviceQueueTest, two_devices_share_process_order_counter) {
    RemoteProcess remote(CommType::MEMORY);
    const std::string first =
        Serialize(PacketHead{PacketType::PEER_ACCESS_RECORD, 0, 0}, PeerAccessRecord{0, 1, PeerAccessOp::ENABLE});
    const std::string second =
        Serialize(PacketHead{PacketType::PEER_ACCESS_RECORD, 0, 0}, PeerAccessRecord{1, 0, PeerAccessOp::ENABLE});
    ASSERT_EQ(LocalDevice::GetInstance(0, CommType::MEMORY).Notify(first), static_cast<ssize_t>(first.size()));
    std::string firstReceived;
    ASSERT_GT(remote.Wait(0, firstReceived), 0);
    ASSERT_EQ(LocalDevice::GetInstance(1, CommType::MEMORY).Notify(second), static_cast<ssize_t>(second.size()));
    std::string secondReceived;
    ASSERT_GT(remote.Wait(0, secondReceived), 0);
    PacketHead firstHead{};
    PacketHead secondHead{};
    ASSERT_TRUE(Deserialize(firstReceived.substr(0, sizeof(firstHead)), firstHead));
    ASSERT_TRUE(Deserialize(secondReceived.substr(0, sizeof(secondHead)), secondHead));
    EXPECT_EQ(secondHead.orderId, firstHead.orderId + 1);
}

TEST_F(LocalDeviceQueueTest, kernel_binary_fallback_uses_same_process_order_counter) {
    RemoteProcess remote(CommType::MEMORY);
    auto &device = LocalDevice::GetInstance(0, CommType::MEMORY);
    const std::string first =
        Serialize(PacketHead{PacketType::PEER_ACCESS_RECORD, 0, 0}, PeerAccessRecord{0, 1, PeerAccessOp::ENABLE});
    ASSERT_EQ(device.Notify(first), static_cast<ssize_t>(first.size()));
    std::string firstReceived;
    ASSERT_GT(remote.Wait(0, firstReceived), 0);

    const std::vector<char> binary{'a', 'b', 'c'};
    ASSERT_GT(device.NotifyKernelBinary(binary), 0);
    std::string binaryReceived;
    ASSERT_GT(remote.Wait(0, binaryReceived), 0);
    ASSERT_EQ(binaryReceived.size(), sizeof(PacketHead) + sizeof(uint64_t) + binary.size());

    PacketHead firstHead{};
    PacketHead binaryHead{};
    ASSERT_TRUE(Deserialize(firstReceived.substr(0, sizeof(firstHead)), firstHead));
    ASSERT_TRUE(Deserialize(binaryReceived.substr(0, sizeof(binaryHead)), binaryHead));
    EXPECT_EQ(binaryHead.type, PacketType::KERNEL_BINARY);
    EXPECT_EQ(binaryHead.orderId, firstHead.orderId + 1);
    uint64_t binarySize = 0;
    std::memcpy(&binarySize, binaryReceived.data() + sizeof(binaryHead), sizeof(binarySize));
    EXPECT_EQ(binarySize, binary.size());
    EXPECT_EQ(
        binaryReceived.substr(sizeof(binaryHead) + sizeof(binarySize)), std::string(binary.begin(), binary.end()));
}

TEST(LocalDevice, forked_child_cannot_reuse_parent_device_connection) {
    (void)LocalDevice::GetInstance(0, CommType::MEMORY);
    testing::internal::CaptureStderr();
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        (void)LocalDevice::GetInstance(0, CommType::MEMORY);
        std::_Exit(0);
    }
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), EXIT_FAILURE);
    const auto reason = testing::internal::GetCapturedStderr();
    EXPECT_NE(reason.find("forked child cannot reuse device connections inherited"), std::string::npos);
}

TEST(LocalDevice, wait_expect_success)
{
    RemoteProcess remote(CommType::MEMORY);
    std::string sendMsg = "hello";
    remote.Notify(0, sendMsg);
    std::string recvMsg = "";
    int32_t mockDeviceId = 0;
    LocalDevice::GetInstance(mockDeviceId, CommType::MEMORY).Wait(recvMsg);
    ASSERT_STREQ(recvMsg.c_str(), sendMsg.c_str());
}

// 两个调用线程共用设备对象；按收到的完整消息验证编号和载荷，不假定抢锁先后。
class ConcurrentLocalDeviceTest : public testing::TestWithParam<bool> {};
TEST_P(ConcurrentLocalDeviceTest, packets_remain_complete) {
    RemoteProcess remote(CommType::MEMORY);
    // 内存通信队列是全局测试替身，清掉其它用例留下的消息后再开始本次发送
    std::string stale;
    while (remote.Wait(0, stale) > 0) {
    }
    auto &device = LocalDevice::GetInstance(0, CommType::MEMORY);
    EXPECT_EQ(&device, &LocalDevice::GetInstance(0, CommType::MEMORY));
    (void)LocalDevice::GetInstance(1, CommType::MEMORY);
    EXPECT_EQ(&device, &LocalDevice::GetInstance(0, CommType::MEMORY));
    std::promise<void> start;
    auto ready = start.get_future().share();
    const std::string message =
        Serialize(PacketHead{PacketType::PEER_ACCESS_RECORD, 0, 0}, PeerAccessRecord{0, 1, PeerAccessOp::ENABLE});
    const std::vector<char> binary(257, 'b');
    std::thread normal([&] {
        ready.wait();
        EXPECT_EQ(&device, &LocalDevice::GetInstance(0, CommType::MEMORY));
        EXPECT_EQ(device.Notify(message), static_cast<ssize_t>(message.size()));
        EXPECT_EQ(device.Notify(message), static_cast<ssize_t>(message.size()));
    });
    std::thread elf([&] {
        ready.wait();
        if (GetParam()) {
            EXPECT_GT(device.NotifyKernelBinary(binary), 0);
        } else {
            EXPECT_EQ(device.Notify(message), static_cast<ssize_t>(message.size()));
        }
    });
    start.set_value();
    normal.join();
    elf.join();
    uint64_t previous = 0;
    int binaries = 0;
    for (int i = 0; i < 3; ++i) {
        std::string received;
        ASSERT_GT(remote.Wait(0, received), 0);
        PacketHead head{};
        ASSERT_TRUE(Deserialize(received.substr(0, sizeof(head)), head));
        EXPECT_GT(head.orderId, 0);
        if (previous != 0) {
            EXPECT_EQ(head.orderId, previous + 1);
        }
        previous = head.orderId;
        if (head.type == PacketType::KERNEL_BINARY) {
            ++binaries;
            ASSERT_EQ(received.size(), sizeof(head) + sizeof(uint64_t) + binary.size());
            uint64_t size = 0;
            std::memcpy(&size, received.data() + sizeof(head), sizeof(size));
            EXPECT_EQ(size, binary.size());
            EXPECT_EQ(received.substr(sizeof(head) + sizeof(size)), std::string(binary.begin(), binary.end()));
        } else {
            EXPECT_EQ(head.type, PacketType::PEER_ACCESS_RECORD);
            EXPECT_EQ(received.substr(sizeof(head)), message.substr(sizeof(head)));
        }
    }
    EXPECT_EQ(binaries, GetParam() ? 1 : 0);
}
INSTANTIATE_TEST_CASE_P(NormalAndBinary, ConcurrentLocalDeviceTest, testing::Values(false, true));

namespace {
struct PausedNotify {
    std::promise<void> firstNumbered;
    std::shared_future<void> release;
    std::mutex mutex;
    std::vector<uint64_t> arrived;
};
PausedNotify *pausedNotify = nullptr;
ssize_t NotifyAfterNumber(LocalProcess *, const std::string &message) {
    PacketHead head{};
    std::memcpy(&head, message.data(), sizeof(head));
    PeerAccessRecord record{};
    std::memcpy(&record, message.data() + sizeof(head), sizeof(record));
    if (record.deviceId == 0) {
        pausedNotify->firstNumbered.set_value();
        pausedNotify->release.wait();
    }
    std::lock_guard<std::mutex> lock(pausedNotify->mutex);
    pausedNotify->arrived.push_back(head.orderId);
    return static_cast<ssize_t>(message.size());
}
}

TEST(LocalDevice, later_device_can_send_while_first_numbered_packet_is_paused) {
    auto &first = LocalDevice::GetInstance(0, CommType::MEMORY);
    auto &second = LocalDevice::GetInstance(1, CommType::MEMORY);
    std::promise<void> release;
    PausedNotify state;
    state.release = release.get_future().share();
    auto numbered = state.firstNumbered.get_future();
    pausedNotify = &state;
    // 编号已在 Notify 内生成，暂停底层字节发送以保持取号后的交错场景
    MOCKER(&LocalProcess::SendBytes).stubs().will(invoke(NotifyAfterNumber));
    auto send = [](LocalDevice &device, int id) {
        auto message = Serialize(
            PacketHead{PacketType::PEER_ACCESS_RECORD, 0, 0}, PeerAccessRecord{id, 1 - id, PeerAccessOp::ENABLE});
        EXPECT_EQ(device.Notify(message), static_cast<ssize_t>(message.size()));
    };
    std::thread thread([&] { send(first, 0); });
    EXPECT_EQ(numbered.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    send(second, 1);
    release.set_value();
    thread.join();
    GlobalMockObject::verify();
    ASSERT_EQ(state.arrived.size(), 2);
    EXPECT_EQ(state.arrived[0], state.arrived[1] + 1);
    pausedNotify = nullptr;
}

// fork后exec重新加载进程状态，不能继续使用父进程的编号和连接对象。
TEST(LocalDevice, exec_starts_new_process_order_counter) {
    if (std::getenv("PEERACCESS_EXEC_CHILD") != nullptr) {
        RemoteProcess remote(CommType::MEMORY);
        auto &device = LocalDevice::GetInstance(0, CommType::MEMORY);
        auto message =
            Serialize(PacketHead{PacketType::PEER_ACCESS_RECORD, 0, 0}, PeerAccessRecord{0, 1, PeerAccessOp::ENABLE});
        ASSERT_GT(device.Notify(message), 0);
        std::string received;
        ASSERT_GT(remote.Wait(0, received), 0);
        PacketHead head{};
        ASSERT_TRUE(Deserialize(received.substr(0, sizeof(head)), head));
        EXPECT_EQ(head.orderId, 1);
        return;
    }
    (void)LocalDevice::GetInstance(0, CommType::MEMORY);
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        setenv("PEERACCESS_EXEC_CHILD", "1", 1);
        execl("/proc/self/exe", "injectionTest", "--gtest_filter=LocalDevice.exec_starts_new_process_order_counter",
            nullptr);
        std::_Exit(127);
    }
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
