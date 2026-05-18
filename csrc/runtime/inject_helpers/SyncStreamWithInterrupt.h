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

#pragma once

#include "acl.h"

/**
 * SIGINT 自定义处理函数
 */
void SigIntHandler(int32_t sigInt);

/**
 * 绑定 SIGINT 到自定义处理函数，在 kernelLaunch 系列接触接口中调用
 */
void BindSigIntHandler(void);

/**
 * 流同步并处理 SIGINT 信号。若同步过程中触发 SIGINT 信号则终止 stream 上的任务
 * @param stream 执行同步的流
 * @return 流同步执行结果
 */
aclError SyncStreamWithInterrupt(aclrtStream stream);

/**
 * kernel 被 SIGINT 终止后的后续处理流程。应在后处理之后调用以保证处理完成进程立即退出
 */
void ExitAfterProcess(void);

#ifdef __BUILD_TESTS__

volatile bool &GetKillRunningKernelFlag();

#endif // __BUILD_TESTS__
