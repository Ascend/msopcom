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


#include "gtest/gtest.h"
#define private public
#include "runtime/inject_helpers/ProfConfig.h"
#undef private
#include <dlfcn.h>
#include "runtime/HijackedFunc.h"
#include "mockcpp/mockcpp.hpp"
#include "runtime/RuntimeOrigin.h"
#include "core/LocalProcess.h"
#include "core/FuncSelector.h"
#include "core/DomainSocket.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/KernelMatcher.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "runtime/inject_helpers/InstrReport.h"
#include "runtime.h"
#include "utils/FileSystem.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"


using namespace std;

TEST(HijackedFuncOfAiCpuKernelLaunchExWithArgsTest, SetKernelContext_WhenProfConfigIsOpProfAndKernelTypeIsAICPU_KFC)
{
    HijackedFuncOfAiCpuKernelLaunchExWithArgs instance;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    instance.Call(KERNEL_TYPE_AICPU_KFC, "testOpMc2AicpuKernel", 1, nullptr, nullptr, 0, 0);
    EXPECT_TRUE(KernelContext::GetAicpuLaunchArgs().isValid);
}

TEST(HijackedFuncOfAiCpuKernelLaunchExWithArgsTest, NotSetKernelContext_WhenKernelTypeIsNotAICPU_KFC)
{
    HijackedFuncOfAiCpuKernelLaunchExWithArgs instance;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    instance.Call(0, "testOp", 1, nullptr, nullptr, 0, 0);
    EXPECT_FALSE(KernelContext::GetAicpuLaunchArgs().isValid);
}
