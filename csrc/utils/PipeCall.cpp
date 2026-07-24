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


#include <algorithm>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

#include "Ustring.h"
#include "InjectLogger.h"

#include "PipeCall.h"

namespace {

/**
 * @desc Pipe 是通过 RAII 机制来管理管道的类
 * 一个管道由两个描述符组成，前面的是读端，后面的是写端
 */
struct Pipe {
public:
    ~Pipe(void)
    {
        if (this->pipe[0] != -1) {
            Close(this->pipe[0]);
        }
        if (this->pipe[1] != -1) {
            Close(this->pipe[1]);
        }
    }

    /**
     * @desc 通过索引获取管道其中一个描述符，并返回描述符的引用，需要调用者自己保证索引有效
     */
    int &operator[](std::size_t idx)
    {
        return this->pipe[idx];
    }

    /**
     * @desc 创建管道
     * @return true 管道创建成功
     *         false 管道创建失败，此时文件描述符为无效状态
     */
    bool Create(void)
    {
        std::lock_guard<std::mutex> guard(mtx);
        bool ret = ::pipe(this->pipe) == 0;
        // 如果管道创建成功，需要把打开的描述符加到全局列表中
        if (ret) {
            fdsForClose.emplace_back(this->pipe[0]);
            fdsForClose.emplace_back(this->pipe[1]);
        }
        return ret;
    }

    /**
     * @desc 关闭一个描述符，并且将其置为 -1
     */
    static void Close(int &fileno)
    {
        std::lock_guard<std::mutex> guard(mtx);
        close(fileno);
        // 关闭描述符后需要将其从全局列表中删除
        auto it = std::find(fdsForClose.begin(), fdsForClose.end(), fileno);
        fdsForClose.erase(it);
        fileno = -1;
    }

    /**
     * @desc 关闭全局列表中的所有描述符，并清空全局列表
     */
    static void CloseAll(void)
    {
        std::lock_guard<std::mutex> guard(mtx);
        for (int fd : fdsForClose) {
            close(fd);
        }
        fdsForClose.clear();
    }

public:
    static std::mutex mtx;

private:
    /**
     * @desc 此处说明为什么需要这个描述符的全局列表
     * 一般来说，一个 PipeCall 的实现大致如下：
     * 1. 在 fork 之前创建好对应输入和输出的管道
     * 2. 通过 fork 创建子进程
     * 3. 在子进程中重定向管道到标准输入和标准输出，关闭所有描述符，执行 `execvp` 进行进程替换
     * 4. 在主进程关闭不需要的描述符，通过管道和子进程进行通信，通信完成后关闭剩余的描述符
     *
     * 这里第一步会创建 2 个管道共 4 个描述符，fork 之后父子进程各自都会持有这 4 个描述符。对于
     * 像 `cat` 这类需要读取标准输入的程序而言，只有当接收到 eof 标志时才会退出，否则会一直阻塞
     * 在 `read` 函数上。管道读端的 eof 是当写端关闭时发送的，因此子进程需要保证在 `execvp` 前
     * 关闭所有的描述符，主进程要在 `waitpid` 前关闭所有的描述符
     *
     * 但是当多个线程同时调用 fork 时事情就会变得很复杂。linux 下的 fork 符合 POSIX 标准，即子
     * 进程只会复制当前调用 fork 的线程的资源，但是会继承父进程所有文件描述符。当两个线程同时创
     * 建了管道后，再同时调用 fork 生成子进程，此时每个子进程除了持有对应线程的描述符，还会持有
     * 兄弟线程的描述符。但由于兄弟线程的资源不会被子进程复制，这些描述符没有地方可以关闭，导致
     * 程序阻塞在 read 函数。因此需要维护一个全局的描述符列表，这些描述符都应该在子进程中关闭
     */
    static std::vector<int> fdsForClose;
    int pipe[2] {-1, -1};
};

std::mutex Pipe::mtx;
std::vector<int> Pipe::fdsForClose;

void Write(int &writeFd, std::string const &input)
{
    std::size_t totalSent = 0;
    ssize_t nBytes = 0;
    while ((nBytes = write(writeFd, input.data() + totalSent, input.size() - totalSent)) > 0) {
        totalSent += static_cast<std::size_t>(nBytes);
    }
    // close writeFd to send eof to child process
    Pipe::Close(writeFd);
}

void Read(int &readFd, std::string &output)
{
    static constexpr std::size_t bufLen = 1024;
    char buf[bufLen] = {'\0'};
    ssize_t nBytes = 0;
    while ((nBytes = read(readFd, buf, bufLen)) > 0) {
        output.append(buf, static_cast<std::size_t>(nBytes));
    }
    Pipe::Close(readFd);
}

inline void PrintCmd(std::vector<std::string> const &cmd)
{
    DEBUG_LOG("PipeCall: %.10240s", Join(cmd.cbegin(), cmd.cend(), " ").c_str());
}

} // namespace [Dummy]

bool PipeCall(std::vector<std::string> const &cmd,
              std::string &output,
              std::string const &input)
{
    PrintCmd(cmd);

    Pipe pipeStdin;
    Pipe pipeStdout;
    if (!pipeStdin.Create() || !pipeStdout.Create()) {
        return false;
    }

    // 在 fock 前后需要加锁，防止一个线程在修改全局描述符列表时另一个线程调用了 fork，
    // 导致子进程拿到的描述符列表状态异常
    pid_t pid {};
    {
        std::lock_guard<std::mutex> guard(Pipe::mtx);
        pid = fork();
    }
    if (pid < 0) {
        return false;
    } else if (pid == 0) {
        unsetenv("LD_PRELOAD");
        dup2(pipeStdin[0], STDIN_FILENO);
        dup2(pipeStdout[1], STDOUT_FILENO);
        // 将标准错误重定向至标准输出
        dup2(STDOUT_FILENO, STDERR_FILENO);

        Pipe::Close(pipeStdin[0]);
        Pipe::Close(pipeStdin[1]);
        Pipe::Close(pipeStdout[0]);
        Pipe::Close(pipeStdout[1]);
        Pipe::CloseAll();

        execvp(cmd[0].c_str(), ToRawCArgv(cmd).data());
        _exit(EXIT_FAILURE);
    } else {
        // close unused fds in parent process
        Pipe::Close(pipeStdin[0]);
        Pipe::Close(pipeStdout[1]);

        // write input to stdin of child process. Write function will be blocked while
        // pipe buffer is full. Thus write input in another thread.
        auto writeAsync = std::thread(Write, std::ref(pipeStdin[1]), input);
        Read(pipeStdout[0], output);

        // wait child process exit
        int status;
        waitpid(pid, &status, 0);
        writeAsync.join();

        // 进程正常结束并且进程结束代码为 0 时认为命令执行成功
        unsigned int unsignedBit = static_cast<unsigned int>(status);
        return WIFEXITED(unsignedBit) && WEXITSTATUS(unsignedBit) == 0;
    }
}
