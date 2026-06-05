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

class HijackedFuncOfAclrtGetDeviceInfoImplTest : public ContextMockHelper {
};

TEST_F(HijackedFuncOfAclrtGetDeviceInfoImplTest, prof_simulator_get_device_info_success)
{
    HijackedFuncOfAclrtGetDeviceInfoImpl inst;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    int64_t value;
    ProfConfig::Instance().socVersion_ = "Ascend910B1";
    ASSERT_EQ(inst.Call(0, ACL_DEV_ATTR_AICORE_CORE_NUM, &value), ACL_SUCCESS);
    ASSERT_EQ(value, 24);
    ASSERT_EQ(inst.Call(0, ACL_DEV_ATTR_VECTOR_CORE_NUM, &value), ACL_SUCCESS);
    ASSERT_EQ(value, 48);

    ProfConfig::Instance().socVersion_ = "Ascend950PR_9599";
    ASSERT_EQ(inst.Call(0, ACL_DEV_ATTR_AICORE_CORE_NUM, &value), ACL_SUCCESS);
    ASSERT_EQ(value, 36);
    ASSERT_EQ(inst.Call(0, ACL_DEV_ATTR_VECTOR_CORE_NUM, &value), ACL_SUCCESS);
    ASSERT_EQ(value, 72);
    FuncSelector::Instance().Set(ToolType::NONE);
    ProfConfig::Instance().profConfig_.isSimulator = false;
}

TEST_F(HijackedFuncOfAclrtGetDeviceInfoImplTest, call_interface_faild_origin_func_is_null)
{
    HijackedFuncOfAclrtGetDeviceInfoImpl instance;
    int64_t value;
    instance.originfunc_ = nullptr;
    aclrtDevAttr attr = ACL_DEV_ATTR_AICORE_CORE_NUM;
    ASSERT_EQ(instance.Call(0, attr, &value), ACL_ERROR_INTERNAL_ERROR);
}
