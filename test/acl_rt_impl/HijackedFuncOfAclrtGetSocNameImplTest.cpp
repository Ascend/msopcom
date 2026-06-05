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
#include "mockcpp/mockcpp.hpp"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "injectors/ContextMockHelper.h"
#include "core/FuncSelector.h"
#define private public
#define protected public
#include "runtime/inject_helpers/ProfConfig.h"
#include "core/HijackedFuncTemplate.h"
#include "acl_rt_impl/HijackedFunc.h"
#undef private
#undef protected
using namespace std;

class HijackedFuncOfAclrtGetSocNameImplTest : public ContextMockHelper {
};

TEST_F(HijackedFuncOfAclrtGetSocNameImplTest, prof_simulator_get_soc_version_success)
{
    HijackedFuncOfAclrtGetSocNameImpl inst;
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = true;
    ProfConfig::Instance().socVersion_ = "Ascend910B1";
    std::string version = inst.Call();
    ASSERT_EQ(version, "Ascend910B1");
    FuncSelector::Instance().Set(ToolType::NONE);
    ProfConfig::Instance().profConfig_.isSimulator = false;
}

TEST_F(HijackedFuncOfAclrtGetSocNameImplTest, sanitizer_get_func_soc_version_success)
{
    HijackedFuncOfAclrtGetSocNameImpl inst;
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    auto func = []() -> const char* {
        return "Ascend910B1";
    };
    inst.originfunc_ = func;
    std::string version = inst.Call();
    ASSERT_EQ(version, "Ascend910B1");
}
