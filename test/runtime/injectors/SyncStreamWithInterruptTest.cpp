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
#include <gtest/gtest.h>
#include "mockcpp/mockcpp.hpp"

#include "runtime/inject_helpers/SyncStreamWithInterrupt.h"

#include "acl_rt_impl/AscendclImplOrigin.h"
#include "acl.h"

bool g_kernelAbort = false;
aclError AclrtStreamAbortStub(aclrtStream stream)
{
    g_kernelAbort = true;
}

bool g_sigintCatch = false;
void SigIntHandlerStub(int signo)
{
    g_sigintCatch = true;
}

TEST(SyncStreamWithInterruptTestSuit, bind_sigint_handler_expect_just_return)
{
    BindSigIntHandler();
    signal(SIGINT, SIG_DFL);
}

TEST(SyncStreamWithInterruptTestSuit, register_sigint_handler_then_raise_sigint_expect_kill_running_kernel)
{
    signal(SIGINT, &SigIntHandler);
    GetKillRunningKernelFlag() = false;

    ASSERT_EQ(raise(SIGINT), 0);
    ASSERT_EQ(GetKillRunningKernelFlag(), true);

    signal(SIGINT, SIG_DFL);
}

TEST(SyncStreamWithInterruptTestSuit, sync_stream_with_kernel_run_failed_expect_return_correct_error_code)
{
    MOCKER(&aclrtSynchronizeStreamWithTimeoutImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));

    ASSERT_EQ(SyncStreamWithInterrupt(nullptr), ACL_ERROR_INTERNAL_ERROR);

    GlobalMockObject::verify();
}

TEST(SyncStreamWithInterruptTestSuit, sync_stream_with_sync_timeout_and_kill_kernel_expect_stream_abort_called)
{
    MOCKER(&aclrtSynchronizeStreamWithTimeoutImplOrigin).stubs().will(returnValue(ACL_ERROR_RT_STREAM_SYNC_TIMEOUT));
    MOCKER(&aclrtStreamAbortImplOrigin).stubs().will(invoke(&AclrtStreamAbortStub));
    GetKillRunningKernelFlag() = true;
    g_kernelAbort = false;

    ASSERT_EQ(SyncStreamWithInterrupt(nullptr), ACL_ERROR_RT_STREAM_SYNC_TIMEOUT);
    ASSERT_EQ(g_kernelAbort, true);

    GlobalMockObject::verify();
}

TEST(SyncStreamWithInterruptTestSuit, exit_after_process_expect_raise_sigint_called)
{
    GetKillRunningKernelFlag() = true;
    g_sigintCatch = false;

    signal(SIGINT, &SigIntHandlerStub);
    ExitAfterProcess();
    ASSERT_EQ(g_sigintCatch, true);

    signal(SIGINT, SIG_DFL);
}
