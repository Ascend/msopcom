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
#include "utils/FileSystem.h"

#include "acl_rt_impl/AscendclImplOrigin.h"
#include "acl.h"
#include "utils/FileSystem.h"

bool g_kernelAbort = false;
aclError AclrtStreamAbortStub(aclrtStream stream)
{
    g_kernelAbort = true;
    return ACL_ERROR_NONE;
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

TEST(SyncStreamWithInterruptTestSuit, register_sigint_handler_then_raise_sigint_expect_kill_running_kernel) {
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

TEST(SyncStreamWithInterruptTestSuit, exit_after_process_expect_raise_sigint_called) {
    GetKillRunningKernelFlag() = true;
    g_sigintCatch = false;

    signal(SIGINT, &SigIntHandlerStub);
    ExitAfterProcess();
    ASSERT_EQ(g_sigintCatch, true);

    signal(SIGINT, SIG_DFL);
}

// 临时路径清理器：AddPath/RemovePath/Contains 基本契约
TEST(SyncStreamWithInterruptTestSuit, temp_file_cleaner_add_and_remove_path_expect_contains_correct) {
    const std::string dir = "./ut_tmp_registry_dir";
    MkdirRecusively(dir);

    TempFileCleaner::Instance().AddPath(dir, false);
    ASSERT_TRUE(TempFileCleaner::Instance().Contains(dir));

    TempFileCleaner::Instance().RemovePath(dir);
    ASSERT_FALSE(TempFileCleaner::Instance().Contains(dir));

    RemoveAll(dir); // 用例自身清理
}

// 回归 issue #84 核心：Ctrl-C 退出路径必须清理所有已登记的 tmp 根目录，
// 而非仅当前线程那一份（DBITaskConfig 为 thread_local）
TEST(SyncStreamWithInterruptTestSuit, exit_after_process_expect_all_registered_tmp_dirs_cleaned) {
    // 模拟两个"线程"各自登记的 tmp 根目录
    const std::string dirA = "./ut_tmp_dir_A";
    const std::string dirB = "./ut_tmp_dir_B";
    MkdirRecusively(dirA);
    MkdirRecusively(dirB);
    TempFileCleaner::Instance().AddPath(dirA, false);
    TempFileCleaner::Instance().AddPath(dirB, false);
    ASSERT_TRUE(IsExist(dirA));
    ASSERT_TRUE(IsExist(dirB));

    GetKillRunningKernelFlag() = true;
    g_sigintCatch = false;
    signal(SIGINT, &SigIntHandlerStub);
    ExitAfterProcess();

    // 两个目录都被清理，且登记表已摘除（CleanAll 后 Size 为 0）
    ASSERT_TRUE(g_sigintCatch);
    ASSERT_FALSE(IsExist(dirA));
    ASSERT_FALSE(IsExist(dirB));
    ASSERT_EQ(TempFileCleaner::Instance().Size(), 0);

    signal(SIGINT, SIG_DFL);
}

// keep=true 的目录按原语义保留，不因 Ctrl-C 清理而被删
TEST(SyncStreamWithInterruptTestSuit, exit_after_process_expect_keep_dir_not_cleaned) {
    const std::string keepDir = "./ut_tmp_keep_dir";
    MkdirRecusively(keepDir);
    TempFileCleaner::Instance().AddPath(keepDir, true);

    GetKillRunningKernelFlag() = true;
    g_sigintCatch = false;
    signal(SIGINT, &SigIntHandlerStub);
    ExitAfterProcess();

    ASSERT_TRUE(g_sigintCatch);
    ASSERT_TRUE(IsExist(keepDir));
    RemoveAll(keepDir); // 用例自身清理

    signal(SIGINT, SIG_DFL);
}
