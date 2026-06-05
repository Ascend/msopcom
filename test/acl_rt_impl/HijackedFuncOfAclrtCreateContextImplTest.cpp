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

class HijackedFuncOfAclrtCreateContextImplTest : public ContextMockHelper {
};

TEST_F(HijackedFuncOfAclrtCreateContextImplTest, call_interface_suc_when_replace_to_0)
{
    HijackedFuncOfAclrtCreateContextImpl inst;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    int32_t deviceId;
    auto func = [](aclrtContext *context, int32_t deviceId) -> aclError { return ACL_SUCCESS; };
    inst.originfunc_ = func;
    ASSERT_EQ(inst.Call(nullptr, deviceId), ACL_SUCCESS);
    FuncSelector::Instance().Set(ToolType::NONE);
    ProfConfig::Instance().profConfig_.isSimulator = false;
}

TEST_F(HijackedFuncOfAclrtCreateContextImplTest, call_interface_faild_origin_func_is_null)
{
    HijackedFuncOfAclrtCreateContextImpl instance;
    int32_t deviceId;
    instance.originfunc_ = nullptr;
    ASSERT_EQ(instance.Call(nullptr, deviceId), ACL_ERROR_INTERNAL_ERROR);
    FuncSelector::Instance().Set(ToolType::NONE);
}
