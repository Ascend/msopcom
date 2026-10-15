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


#ifndef __CORE_DOMAIN_SOCKET_H__
#define __CORE_DOMAIN_SOCKET_H__

#include <mutex>
#include <string>
#include <sys/types.h>
#include <sys/un.h>
#include <vector>

// DomainSocket 类提供基于 domain socket 的通信功能，基于 socket 的特性支持全双工
// 服务端和客户端的调用流程如下：
// server: DomainSocket() -> ListenAndBind() -> Accept() -> Read/Write()
// client: DomainSocket() -> Connect() -> Read/Write()
class DomainSocket {
public:
    explicit DomainSocket(const std::string &socketPath);
    virtual ~DomainSocket();

protected:
    bool CreateSocket();

    std::string socketPath_;
    int32_t sfd_ { -1 };
    sockaddr_un addr_ { 0 };
};

class DomainSocketServer : public DomainSocket {
public:
    using ClientId = std::size_t;

    explicit DomainSocketServer(const std::string &socketPath, std::size_t maxClientNum);
    ~DomainSocketServer() override;

    /** 设置路径 socket 路径绑定和监听属性
     * @description 此接口返回 false 一般是由于路径已经被其他 socket 绑定导致
     */
    bool ListenAndBind();

    /** 等待客户端连接
     * @description 如果当前无客户端连接则会直接返回 false，调用者需要自己处理
     * accept 失败重试的情况
     * @param id 连接成功则会返回当前连接上的客户端 id，否则为无效值
     */
    bool Accept(ClientId &id);

    /** 获取当前已连接的客户端数量
     */
    std::size_t GetClientNum() const;

    /** 从指定客户端读取数据
     * @description 此接口返回失败一般会有几种情况：
     * 1. 指定的 id 超出了已连接客户端列表的范围
     * 2. 读取等待超时
     * 3. 对端已关闭（EOF）
     * @param id 要读取的客户端 id
     * @param message 读取到的字符串
     * @param maxBytes 一次读取的最大字节数
     * @param receiveBytes 实际读取的字节数
     */
    bool Read(ClientId id, std::string &message, size_t maxBytes, size_t &receivedBytes);

    /** 向指定客户端写入数据
     * @description 此接口返回失败一般会有几种情况：
     * 1. 指定的 id 超出了已连接客户端列表的范围
     * 2. 对端已关闭（EOF）
     * @param id 要写入的客户端 id
     * @param message 要写入的字符串
     * @param sentBytes 实际写入的字节数
     */
    bool Write(ClientId id, const std::string &message, size_t &sentBytes);

protected:
    std::size_t maxClientNum_;
    std::vector<int32_t> cfds_;
    std::mutex cfdsMutex_;
};

class DomainSocketClient : public DomainSocket {
public:
    explicit DomainSocketClient(const std::string &socketPath);
    ~DomainSocketClient() override;

    /** 连接服务端
     * @description 此接口返回失败一般是由于服务端还未绑定 socket 路径
     * 导致，需要调用者处理重试
     */
    bool Connect();

    /** 从服务端读取数据
     * @description 此接口返回失败一般会有几种情况：
     * 1. 读取等待超时
     * 2. 对端已关闭（EOF）
     * @param message 读取到的字符串
     * @param maxBytes 一次读取的最大字节数
     * @param receiveBytes 实际读取的字节数
     */
    bool Read(std::string &message, size_t maxBytes, size_t &receivedBytes) const;

    /** 向服务端写入数据
     * @description 此接口返回失败一般是由于对端已关闭（EOF）
     * @param message 要写入的字符串
     * @param sentBytes 实际写入的字节数
     */
    bool Write(const std::string &message, size_t &sentBytes);

    /** 发送数据并附带上一个 fd（SCM_RIGHTS），用于内核二进制零拷贝传输 */
    bool WriteWithFd(const std::string &message, int fd, size_t &sentBytes);
    std::mutex mux_;
};

#endif // __CORE_DOMAIN_SOCKET__
