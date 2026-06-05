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
#include "profapi/HijackedFunc.h"
#undef private
#undef protected
#include "core/FuncSelector.h"
#include "profapi/ProfInjectHelper.h"

int g_callbackTime = 0;
int32_t CallbackHandle(uint32_t type, void *data, uint32_t len)
{
    g_callbackTime++;
    return 0;
}

/**
 * |  用例集 | HijackedFuncOfMsprofRegisterCallbackTest
 * | 测试函数 | HijackedFuncOfMsprofRegisterCallback::Pre
 * |  用例名 | aicore_module_call_handle_one_time
 * | 用例描述 | 测试module为aicore 61时会主动调用handle回调函数，且在handleMap增加相应module数据
 */

TEST(HijackedFuncOfMsprofRegisterCallbackTest, aicore_module_call_handle_one_time)
{
    g_callbackTime = 0;
    FuncSelector::Instance().Set(ToolType::PROF);

    HijackedFuncOfMsprofRegisterCallback instance;
    uint32_t module = 61;
    ProfCommandHandle handle = &CallbackHandle;
    instance.Pre(module, handle);
    ASSERT_EQ(g_callbackTime, 1);
    ASSERT_TRUE(ProfInjectHelper::Instance().handleMap_.find(module) != ProfInjectHelper::Instance().handleMap_.end());
}

/**
 * |  用例集 | HijackedFuncOfMsprofRegisterCallbackTest
 * | 测试函数 | HijackedFuncOfMsprofRegisterCallback::Pre
 * |  用例名 | other_module_do_not_call_handle
 * | 用例描述 | 测试module不为aicore 61时不会调用handle回调函数，但会在handleMap增加相应module数据
 */
TEST(HijackedFuncOfMsprofRegisterCallbackTest, other_module_do_not_call_handle)
{
    g_callbackTime = 0;
    FuncSelector::Instance().Set(ToolType::PROF);

    HijackedFuncOfMsprofRegisterCallback instance;
    uint32_t module = 0;
    ProfCommandHandle handle = &CallbackHandle;
    instance.Pre(module, handle);
    ASSERT_EQ(g_callbackTime, 0);
    ASSERT_TRUE(ProfInjectHelper::Instance().handleMap_.find(module) != ProfInjectHelper::Instance().handleMap_.end());
}

/**
 * |  用例集 | HijackedFuncOfMsprofRegisterCallbackTest
 * | 测试函数 | HijackedFuncOfMsprofRegisterCallback::Pre
 * |  用例名 | not_opprof_do_not_call_handle
 * | 用例描述 | 测试非OpProf情况下不会调用handle回调函数，也不会在handleMap增加相应module数据
 */
TEST(HijackedFuncOfMsprofRegisterCallbackTest, not_opprof_do_not_call_handle)
{
    g_callbackTime = 0;
    FuncSelector::Instance().Set(ToolType::NONE);

    HijackedFuncOfMsprofRegisterCallback instance;
    uint32_t module = 1;
    ProfCommandHandle handle = &CallbackHandle;
    instance.Pre(module, handle);
    ASSERT_EQ(g_callbackTime, 0);
    ASSERT_TRUE(ProfInjectHelper::Instance().handleMap_.find(module) == ProfInjectHelper::Instance().handleMap_.end());
}
