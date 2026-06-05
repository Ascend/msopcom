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


#include <gtest/gtest.h>
#include <cstdlib>
#include <dlfcn.h>
#include "mockcpp/mockcpp.hpp"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "injectors/ContextMockHelper.h"
#include "core/FuncSelector.h"
#define private public
#define protected public
#include "runtime/inject_helpers/ProfConfig.h"
#include "core/HijackedFuncTemplate.h"
#undef private
#undef protected
#include "acl_rt_impl/HijackedFunc.h"
using namespace std;

class HijackedFuncOfAclrtQueryDeviceStatusImplTest : public ContextMockHelper {
};

TEST_F(HijackedFuncOfAclrtQueryDeviceStatusImplTest, prof_simulator_get_soc_version_suc)
{
    HijackedFuncOfAclrtQueryDeviceStatusImpl inst;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    auto func = [](int32_t deviceId, aclrtDeviceStatus *deviceStatus) -> aclError { return ACL_SUCCESS; };
    inst.originfunc_ = func;
    ASSERT_EQ(inst.Call(0, nullptr), ACL_SUCCESS);
    FuncSelector::Instance().Set(ToolType::NONE);
    ProfConfig::Instance().profConfig_.isSimulator = false;
}

TEST_F(HijackedFuncOfAclrtQueryDeviceStatusImplTest, call_interface_faild_dlopen_failed)
{
    MOCKER(&dlopen).stubs().will(returnValue(nullptr));
    HijackedFuncOfAclrtQueryDeviceStatusImpl instance;
    aclrtContext *context;
    int32_t deviceId;
    ASSERT_EQ(instance.Call(0, nullptr), ACL_ERROR_INTERNAL_ERROR);
    FuncSelector::Instance().Set(ToolType::NONE);
    GlobalMockObject::verify();
}
