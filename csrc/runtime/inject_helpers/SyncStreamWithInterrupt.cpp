/* -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
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

#include <csignal>
#include <iostream>
#include <mutex>
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "utils/InjectLogger.h"
#include "utils/signal.h"
#include "SyncStreamWithInterrupt.h"

namespace {

volatile bool g_killRunningKernel = false;

} // namespace [Dummy]

void SigIntHandler(int32_t signo) {
    printf("Ctrl-C received. Running kernel will be killed, and you can press Ctrl-C again to force quit.\n");
    fflush(stdout);

    g_killRunningKernel = true;

    // 自定义的 SIGINT 期望仅触发一次，之后恢复系统默认的处理函数，以保证用户第二次 Ctrl-C 的时候能正常退出
    SignalWrapper::UnregisterCallback(SIGINT);
}

void BindSigIntHandler(void) {
    // 使用 once_flag 和 call_once 保证自定义的 SIGINT 全局仅调用一次
    static std::once_flag sigIntRegOnce;
    std::call_once(sigIntRegOnce, [&]() {
        SignalWrapper::RegisterCallback(SIGINT, SigIntHandler);
    });
}

aclError SyncStreamWithInterrupt(aclrtStream stream) {
    aclError ret = ACL_ERROR_NONE;
    // 流同步每次等待 1s，最大等待次数只需比硬件超时时间长即可，硬件超时时算子会强制退出
    constexpr size_t syncStreamMaxRetry = 2400;
    constexpr int32_t syncStreamTimeout = 1000;

    for (size_t i = 0; i < syncStreamMaxRetry; ++i) {
        ret = aclrtSynchronizeStreamWithTimeoutImplOrigin(stream, syncStreamTimeout);
        // 如果返回的不是流同步超时，说明算子已经执行完毕，可以直接返回
        if (ret != ACL_ERROR_RT_STREAM_SYNC_TIMEOUT) {
            DEBUG_LOG("aclrtSynchronizeStreamWithTimeoutImplOrigin ret %d", ret);
            return ret;
        }

        // 如果流同步超时，检查用户是否执行过 Ctrl-C，若执行过则强制终止任务
        if (g_killRunningKernel) {
            aclError abortRet = aclrtStreamAbortImplOrigin(stream);
            DEBUG_LOG("aclrtStreamAbortImplOrigin ret %d", abortRet);
            return ret;
        }
    }

    return ret;
}

void ExitAfterProcess(void) {
    // 若当前 kernel 已经被 Ctrl-C，则在处理完当前 kernel 数据后直接退出
    if (g_killRunningKernel) {
        int ret = raise(SIGINT);
        if (ret != 0) {
            ERROR_LOG("raise SIGINT failed ret %d", ret);
        }
    }
}

#ifdef __BUILD_TESTS__

volatile bool &GetKillRunningKernelFlag() { return g_killRunningKernel; }

#endif // __BUILD_TESTS__
