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


#include "HijackedFunc.h"

#include <elf.h>
#include <string>
#include "utils/FileSystem.h"
#include "core/LocalProcess.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "core/PlatformConfig.h"
#include "core/BinaryInstrumentation.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "runtime/inject_helpers/DBITask.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/InstrReport.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ConfigManager.h"
#include "runtime/inject_helpers/LaunchArgs.h"
#include "runtime/inject_helpers/MemGuard.h"
#include "runtime/inject_helpers/SyncStreamWithInterrupt.h"
#include "RuntimeConfig.h"
#include "utils/Ustring.h"
using namespace std;

HijackedFuncOfAiCpuKernelLaunchExWithArgs::HijackedFuncOfAiCpuKernelLaunchExWithArgs()
    : HijackedFuncOfAiCpuKernelLaunchExWithArgs::HijackedFuncType(
    std::string(RuntimeLibName()), std::string("rtAicpuKernelLaunchExWithArgs")) {}


void HijackedFuncOfAiCpuKernelLaunchExWithArgs::Pre(const uint32_t kernelType, const char *const opName,
    const uint32_t blockDim, const rtAicpuArgsEx_t *argsInfo, rtSmDesc_t *const smDesc, const rtStream_t stm,
    const uint32_t flags)
{
    if (!IsOpProf()) {
        return;
    }

    // mssanitizer SIGINT 信号处理接管
    BindSigIntHandler();

    uint64_t validLen = GetValidLength(opName, KERNEL_NAME_MAX);
    std::string validOpName(opName, validLen);

    if (kernelType == KERNEL_TYPE_AICPU_KFC && EndsWith(validOpName, MC2_AICPU_SUFFIX)) {
        KernelContext::GetAicpuLaunchArgs().kernelType = kernelType;
        KernelContext::GetAicpuLaunchArgs().opName = validOpName; // opname如果传指针解出来的数据会有问题
        KernelContext::GetAicpuLaunchArgs().blockDim = blockDim;
        KernelContext::GetAicpuLaunchArgs().argsInfo = argsInfo;
        KernelContext::GetAicpuLaunchArgs().smDesc = smDesc;
        KernelContext::GetAicpuLaunchArgs().stm = stm;
        KernelContext::GetAicpuLaunchArgs().flags = flags;
        KernelContext::GetAicpuLaunchArgs().isValid = true;
    } else {
        KernelContext::GetAicpuLaunchArgs().isValid = false;
    }

    MemoryGuard::Instance().FillAllMemGuard();
}

rtError_t HijackedFuncOfAiCpuKernelLaunchExWithArgs::Post(rtError_t ret)
{
    if (IsSanitizer()) {
        MemoryGuard::Instance().CheckAllMemGuard();

        ExitAfterProcess();
    }

    return ret;
}
