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
#include "include/thirdparty/prof.h"
#include "profapi/ProfInjectHelper.h"

int g_setDeviceTime = 0;
int32_t SetDeviceHandle(uint32_t type, void *data, uint32_t len)
{
    g_setDeviceTime++;
    return 0;
}

/**
 * |  用例集 | HijackedFuncOfMsprofNotifySetDeviceTest
 * | 测试函数 | HijackedFuncOfMsprofNotifySetDeviceTest::Pre
 * |  用例名 | aicpu_module_init_call_handle_one_time
 * | 用例描述 | 测试module为aicpu 7时会主动调用handle回调函数
 */
TEST(HijackedFuncOfMsprofNotifySetDeviceTest, aicpu_module_init_call_handle_one_time)
{
    g_setDeviceTime = 0;
    ProfCommandHandle handle = &SetDeviceHandle;
    ProfInjectHelper::Instance().handleMap_ = {{AICPU_MODULE_ID, handle}};
    uint32_t deviceId = 0;
    ProfInjectHelper::Instance().aicpuHandleCallMap_ = {{deviceId, false}};
    FuncSelector::Instance().Set(ToolType::PROF);

    HijackedFuncOfMsprofNotifySetDevice instance;
    instance.Pre(0, deviceId, true);
    ASSERT_EQ(g_setDeviceTime, 1);
    ASSERT_TRUE(ProfInjectHelper::Instance().aicpuHandleCallMap_[deviceId]);
}

/**
 * |  用例集 | HijackedFuncOfMsprofNotifySetDeviceTest
 * | 测试函数 | HijackedFuncOfMsprofNotifySetDeviceTest::Pre
 * |  用例名 | aicpu_module_already_init_do_not_call_handle
 * | 用例描述 | 测试module为aicpu 7且已初始化时，不会调用handle回调函数
 */
TEST(HijackedFuncOfMsprofNotifySetDeviceTest, aicpu_module_already_init_do_not_call_handle)
{
    g_setDeviceTime = 0;
    ProfCommandHandle handle = &SetDeviceHandle;
    ProfInjectHelper::Instance().handleMap_ = {{AICPU_MODULE_ID, handle}};
    uint32_t deviceId = 0;
    ProfInjectHelper::Instance().aicpuHandleCallMap_ = {{deviceId, true}};
    FuncSelector::Instance().Set(ToolType::PROF);

    HijackedFuncOfMsprofNotifySetDevice instance;
    instance.Pre(0, deviceId, true);
    ASSERT_EQ(g_setDeviceTime, 0);
    ASSERT_TRUE(ProfInjectHelper::Instance().aicpuHandleCallMap_[deviceId]);
}

/**
 * |  用例集 | HijackedFuncOfMsprofNotifySetDeviceTest
 * | 测试函数 | HijackedFuncOfMsprofNotifySetDeviceTest::Pre
 * |  用例名 | aicpu_module_not_in_handle_map_do_not_call_handle
 * | 用例描述 | 测试module为aicpu 7且不在handleMap时，不会调用handle回调函数
 */
TEST(HijackedFuncOfMsprofNotifySetDeviceTest, aicpu_module_not_in_handle_map_do_not_call_handle)
{
    g_setDeviceTime = 0;
    ProfInjectHelper::Instance().handleMap_ = {};
    uint32_t deviceId = 0;
    ProfInjectHelper::Instance().aicpuHandleCallMap_ = {{deviceId, false}};
    FuncSelector::Instance().Set(ToolType::PROF);

    HijackedFuncOfMsprofNotifySetDevice instance;
    instance.Pre(0, deviceId, true);
    ASSERT_EQ(g_setDeviceTime, 0);
    ASSERT_FALSE(ProfInjectHelper::Instance().aicpuHandleCallMap_[deviceId]);
}

/**
 * |  用例集 | HijackedFuncOfMsprofNotifySetDeviceTest
 * | 测试函数 | HijackedFuncOfMsprofNotifySetDeviceTest::Pre
 * |  用例名 | is_open_is_false_do_not_call_handle
 * | 用例描述 | 测试isOpen参数为false，不会调用handle回调函数
 */
TEST(HijackedFuncOfMsprofNotifySetDeviceTest, is_open_is_false_do_not_call_handle)
{
    g_setDeviceTime = 0;
    ProfCommandHandle handle = &SetDeviceHandle;
    ProfInjectHelper::Instance().handleMap_ = {{AICPU_MODULE_ID, handle}};
    uint32_t deviceId = 0;
    ProfInjectHelper::Instance().aicpuHandleCallMap_ = {{deviceId, false}};
    FuncSelector::Instance().Set(ToolType::PROF);

    HijackedFuncOfMsprofNotifySetDevice instance;
    instance.Pre(0, deviceId, false);
    ASSERT_EQ(g_setDeviceTime, 0);
    ASSERT_FALSE(ProfInjectHelper::Instance().aicpuHandleCallMap_[deviceId]);
}

/**
 * |  用例集 | HijackedFuncOfMsprofNotifySetDeviceTest
 * | 测试函数 | HijackedFuncOfMsprofNotifySetDeviceTest::Pre
 * |  用例名 | not_opprof_do_not_call_handle
 * | 用例描述 | 测试非OpProf情况下不会调用handle回调函数
 */
TEST(HijackedFuncOfMsprofNotifySetDeviceTest, not_opprof_do_not_call_handle)
{
    g_setDeviceTime = 0;
    ProfCommandHandle handle = &SetDeviceHandle;
    ProfInjectHelper::Instance().handleMap_ = {{AICPU_MODULE_ID, handle}};
    uint32_t deviceId = 0;
    ProfInjectHelper::Instance().aicpuHandleCallMap_ = {{deviceId, false}};
    FuncSelector::Instance().Set(ToolType::NONE);

    HijackedFuncOfMsprofNotifySetDevice instance;
    instance.Pre(0, deviceId, true);
    ASSERT_EQ(g_setDeviceTime, 0);
    ASSERT_FALSE(ProfInjectHelper::Instance().aicpuHandleCallMap_[deviceId]);
}
