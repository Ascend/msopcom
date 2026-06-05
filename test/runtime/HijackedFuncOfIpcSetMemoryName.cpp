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
#include "mockcpp/mockcpp.hpp"
#define private public
#define protected public
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/HijackedFunc.h"
#undef private
#undef protected

#include "runtime/inject_helpers/KernelContext.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "core/DomainSocket.h"

TEST(HijackedFuncOfIpcSetMemoryNameTest, normal_calling)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    HijackedFuncOfIpcSetMemoryName instance;
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    const void *ptr = reinterpret_cast<void*>(0x12005810000);
    uint64_t byteCount = 1024;
    char *name = "IPC_MEM_NAME_01234";
    uint32_t len = 65;

    ASSERT_EQ(instance.Call(ptr, byteCount, name, len), RT_ERROR_RESERVED);

    auto func = [](const void *ptr, uint64_t byteCount, char *name, uint32_t len) -> rtError_t {
        return RT_ERROR_NONE;
    };
    instance.originfunc_ = func;
    ASSERT_EQ(instance.Call(ptr, byteCount, name, len), RT_ERROR_NONE);
}

TEST(HijackedFuncOfIpcSetMemoryNameTest, origin_bad_calling)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    HijackedFuncOfIpcSetMemoryName instance;
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    const void *ptr = reinterpret_cast<void*>(0x12005810000);
    uint64_t byteCount = 1024;
    char *name = "IPC_MEM_NAME_01234";
    uint32_t len = 65;

    ASSERT_EQ(instance.Call(ptr, byteCount, name, len), RT_ERROR_RESERVED);

    auto func = [](const void *ptr, uint64_t byteCount, char *name, uint32_t len) -> rtError_t {
        return RT_ERROR_INVALID_VALUE;
    };
    instance.originfunc_ = func;
    ASSERT_EQ(instance.Call(ptr, byteCount, name, len), RT_ERROR_INVALID_VALUE);
}
