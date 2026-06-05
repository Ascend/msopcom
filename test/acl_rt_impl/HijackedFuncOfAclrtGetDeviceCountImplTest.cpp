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

class HijackedFuncOfAclrtGetDeviceCountImplTest : public ContextMockHelper {
};

TEST_F(HijackedFuncOfAclrtGetDeviceCountImplTest, get_16_when_prof_simulator)
{
    HijackedFuncOfAclrtGetDeviceCountImpl inst;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    uint32_t count;
    ASSERT_EQ(inst.Call(&count), ACL_SUCCESS);
    ASSERT_EQ(count, 16);
    FuncSelector::Instance().Set(ToolType::NONE);
    ProfConfig::Instance().profConfig_.isSimulator = false;
}

TEST_F(HijackedFuncOfAclrtGetDeviceCountImplTest, call_interface_faild_origin_func_is_null)
{
    HijackedFuncOfAclrtGetDeviceCountImpl instance;
    uint32_t count;
    instance.originfunc_ = nullptr;
    ASSERT_EQ(instance.Call(&count), ACL_ERROR_INTERNAL_ERROR);
    FuncSelector::Instance().Set(ToolType::NONE);
}

TEST_F(HijackedFuncOfAclrtGetDeviceCountImplTest, call_original_expect_success)
{
    HijackedFuncOfAclrtGetDeviceCountImpl inst;
    FuncSelector::Instance().Set(ToolType::TEST);
    auto func = [](uint32_t *count) -> aclError { return ACL_SUCCESS; };
    inst.originfunc_ = func;
    uint32_t count;
    ASSERT_EQ(inst.Call(&count), ACL_SUCCESS);
    FuncSelector::Instance().Set(ToolType::NONE);
}
