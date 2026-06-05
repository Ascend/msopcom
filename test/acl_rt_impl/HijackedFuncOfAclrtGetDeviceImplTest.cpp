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

class HijackedFuncOfAclrtGetDeviceImplTest : public ContextMockHelper {
};

TEST_F(HijackedFuncOfAclrtGetDeviceImplTest, prof_simulator_get_device_id_suc)
{
    HijackedFuncOfAclrtGetDeviceImpl inst;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    auto func = [](int32_t *a) -> aclError { return ACL_SUCCESS; };
    inst.originfunc_ = func;
    int32_t devId;
    ASSERT_EQ(inst.Call(&devId), ACL_SUCCESS);
    FuncSelector::Instance().Set(ToolType::NONE);
    ProfConfig::Instance().profConfig_.isSimulator = false;
}

TEST_F(HijackedFuncOfAclrtGetDeviceImplTest, call_interface_faild_origin_func_is_null)
{
    HijackedFuncOfAclrtGetDeviceImpl instance;
    int32_t devId;
    instance.originfunc_ = nullptr;
    ASSERT_EQ(instance.Call(&devId), ACL_ERROR_INTERNAL_ERROR);
    FuncSelector::Instance().Set(ToolType::NONE);
}

TEST_F(HijackedFuncOfAclrtGetDeviceImplTest, call_not_prof_expect_return_success)
{
    HijackedFuncOfAclrtGetDeviceImpl inst;
    FuncSelector::Instance().Set(ToolType::TEST);
    auto func = [](int32_t *a) -> aclError { return ACL_SUCCESS; };
    inst.originfunc_ = func;
    int32_t devId;
    ASSERT_EQ(inst.Call(&devId), ACL_SUCCESS);
    FuncSelector::Instance().Set(ToolType::NONE);
}
