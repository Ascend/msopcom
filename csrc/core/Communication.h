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


#ifndef __CORE_COMMUNICATION_H__
#define __CORE_COMMUNICATION_H__

#include <string>
#include <memory>
#include <functional>

enum class CommType {
    MEMORY,     // 利用本地内存模拟
    SOCKET,     // 利用socket实现
    MAX         // 空
};

constexpr size_t RECV_BUFF_SIZE = 1024ULL;
using ClientId = std::size_t;
using ClientConnectHook = std::function<void(ClientId)>;
using ClientMsgHandlerHook = std::function<void(ClientId&, std::string&)>;

// Communication类本身提供进程间通信的基础能力，需要考虑如下：
// 1. 应用进程执行时使能劫持能力，然后，启动工具;
// 2. 工具拉起应用进程
// 考虑本身这套体系是配合工具的，所以，如果使用了资源，比如：共享内存，或者 服务。
// 建议统一在工具侧承载，整个通信组件需要支持阻塞等待，超时失败的能力。
// 维测能力：
// 1. 提供通道的监控能力
class CommunicationServer {
public:
    CommunicationServer() {}
    virtual ~CommunicationServer() = default;

    /** 启动服务端
     * @description 手动启动服务端，在此之前可以设置 SetClientConnectHook
     * 回调，防止回调设置前一些客户端已经连接
     */
    virtual bool Start() = 0;

    /** 从客户端读取数据
     * @description 当客户端未写入数据时阻塞，目前超时时间固定为 1s
     * @param clientId 要读取的客户端 id
     * @param msg 读取到的数据，当接口返回 -1 时为无效值
     * @return -1 表示读取失败或超时
     *         >0 表示读取成功，并返回读取到的数据长度
     */
    virtual ssize_t Read(ClientId clientId, std::string &msg) = 0;

    /** 向客户端写入数据
     * @description 当缓冲区满时阻塞
     * @param clientId 要写入的客户端 id
     * @param msg 要写入的数据
     * @return -1 表示写入失败
     *         >0 表示写入成功，并返回已写入的数据长度
     */
    virtual ssize_t Write(ClientId clientId, std::string const &msg) = 0;

    /** 设置客户端连接通知回调函数
     * @description 当有新客户端连接时，func 回调会被调用，并传入新客户端的
     * id。需要注意回调函数是在一个另一个线程中被调用，如果回调函数中捕获了
     * 其他变量，需要调用者自己在回调函数中对变量加锁处理线程竞争问题
     * @param func 通知回调函数
     */
    virtual void SetClientConnectHook(ClientConnectHook &&hook) = 0;

    /** 设置客户端消息接收通知的回调函数
     * @description 当有客户端发送消息时，hook回调会被调用，服务端即可接收到来自客户端的消息，
     * 回调函数会在多个线程中被调用，所以需要调用者自己在回调函数中对变量加锁处理线程竞争问题
     * @param func 通知回调函数
     */
    virtual void SetMsgHandlerHook(ClientMsgHandlerHook &&hook) = 0;
}; // class CommunicationServer

class CommunicationClient {
public:
    CommunicationClient() {}
    virtual ~CommunicationClient() = default;

    /** 连接服务端
     * @description 服务端未启动时连接会失败，需要调用者自行处理重试
     */
    virtual bool Connect() = 0;

    /** 从服务端读取数据
     * @description 当服务端未写入数据时阻塞，目前超时时间固定为 1s
     * @param msg 读取到的数据，当接口返回 -1 时为无效值
     * @return -1 表示读取失败或超时
     *         >0 表示读取成功，并返回读取到的数据长度
     */
    virtual ssize_t Read(std::string &msg) = 0;

    /** 向服务端写入数据
     * @description 当缓冲区满时阻塞
     * @param msg 要写入的数据
     * @return -1 表示写入失败
     *         >0 表示写入成功，并返回已写入的数据长度
     */
    virtual ssize_t Write(std::string const &msg) = 0;

    /** 发送数据并附带上一个 fd（SCM_RIGHTS）；默认实现不支持，返回 -1（用于内存模式回退） */
    virtual ssize_t WriteWithFd(std::string const &msg, int fd) {
        (void)msg;
        (void)fd;
        return -1;
    }
}; // class CommunicationClient

class Server : public CommunicationServer {
public:
    explicit Server(CommType type);
    ~Server() override;
    bool Start() override;
    ssize_t Read(ClientId clientId, std::string &msg) override;
    ssize_t Write(ClientId clientId, std::string const &msg) override;
    void SetClientConnectHook(ClientConnectHook &&hook) override;
    void SetMsgHandlerHook(ClientMsgHandlerHook &&hook) override;
private:
    CommType type_;
    std::unique_ptr<CommunicationServer> socketServer_;
}; // class Server

class Client : public CommunicationClient {
public:
    explicit Client(CommType type);
    ~Client() override;
    bool Connect() override;
    ssize_t Read(std::string &msg) override;
    ssize_t Write(std::string const &msg) override;
    ssize_t WriteWithFd(std::string const &msg, int fd) override;

private:
    CommType type_;
    std::unique_ptr<CommunicationClient> socketClient_;
}; // class Client

#endif // __CORE_COMMUNICATION_H__
