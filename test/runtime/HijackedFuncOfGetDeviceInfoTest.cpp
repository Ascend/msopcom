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

TEST(HijackedFuncOfGetDeviceInfoTest, prof_simulator_get_device_info_success)
{
    HijackedFuncOfGetDeviceInfo instance;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    uint32_t deviceId = 0;
    int32_t moduleType = 0;
    int32_t infoType = 0;
    int64_t value;
    ProfConfig::Instance().socVersion_ = "Ascend910B1";
    ASSERT_EQ(instance.Call(deviceId, static_cast<int32_t>(tagRtDeviceModuleType::RT_MODULE_TYPE_AICORE), infoType, &value), RT_ERROR_NONE);
    ASSERT_EQ(value, 24);
    ASSERT_EQ(instance.Call(deviceId, static_cast<int32_t>(tagRtDeviceModuleType::RT_MODULE_TYPE_VECTOR_CORE), infoType, &value), RT_ERROR_NONE);
    ASSERT_EQ(value, 48);

    ProfConfig::Instance().socVersion_ = "Ascend950PR_9599";
    ASSERT_EQ(instance.Call(deviceId, static_cast<int32_t>(tagRtDeviceModuleType::RT_MODULE_TYPE_AICORE), infoType, &value), RT_ERROR_NONE);
    ASSERT_EQ(value, 36);
    ASSERT_EQ(instance.Call(deviceId, static_cast<int32_t>(tagRtDeviceModuleType::RT_MODULE_TYPE_VECTOR_CORE), infoType, &value), RT_ERROR_NONE);
    ASSERT_EQ(value, 72);
    FuncSelector::Instance().Set(ToolType::NONE);
    ProfConfig::Instance().profConfig_.isSimulator = false;
}

TEST(HijackedFuncOfGetDeviceInfoTest, isNotSimulator)
{
    HijackedFuncOfGetDeviceInfo instance;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    uint32_t deviceId = 0;
    int32_t moduleType = 0;
    int32_t infoType = 0;
    int64_t *val = nullptr;

    ASSERT_EQ(instance.Call(deviceId, moduleType, infoType, val), RT_ERROR_RESERVED);
    FuncSelector::Instance().Set(ToolType::NONE);
}

TEST(HijackedFuncOfGetDeviceInfoTest, isSimulator_with_originfunc)
{
    HijackedFuncOfGetDeviceInfo instance;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    uint32_t deviceId = 0;
    int32_t moduleType = 0;
    int32_t infoType = 0;
    int64_t *val = nullptr;
    auto func = [](uint32_t, int32_t, int32_t, int64_t*) ->
            rtError_t { return RT_ERROR_NONE; };
    instance.originfunc_ = func;
    ASSERT_EQ(instance.Call(deviceId, moduleType, infoType, val), RT_ERROR_NONE);
    FuncSelector::Instance().Set(ToolType::NONE);
}
