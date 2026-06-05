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
#include "acl_rt_impl/HijackedFunc.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/RegisterManager.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ProfDataCollect.h"

class BinaryLoadFromFileTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        AscendclImplOriginCtor();
    }

    void SetUp() override {}

    void TearDown() override
    {
        GlobalMockObject::verify();
        FuncSelector::Instance().Reset(ToolType::TEST);
        unsetenv(DEVICE_TO_SIMULATOR);
    }
};

TEST_F(BinaryLoadFromFileTest, fake_input_and_mock_prof_then_test_call_expect_ok)
{
    HijackedFuncOfAclrtBinaryLoadFromFileImpl inst;
    auto sp = std::make_shared<RegisterContext>();
    MOCKER_CPP(&RegisterManager::CreateContext,
               RegisterContextSP(RegisterManager::*)(const char*, aclrtBinHandle, uint32_t, aclrtBinaryLoadOptions *)).stubs().will(returnValue(sp));
    MOCKER_CPP(&ProfConfig::IsSimulator).stubs().will(returnValue(true));
    MOCKER_CPP(&ProfDataCollect::SaveObject, bool(*)(const RegisterContextSP &)).stubs().will(returnValue(true));

    FuncSelector::Instance().Set(ToolType::PROF);
    setenv(DEVICE_TO_SIMULATOR, "true", 1);
    const char *path = "empty.o";
    aclrtBinaryLoadOptions options{};
    aclrtBinHandle binHandle;
    ASSERT_EQ(inst.Call(path, &options, &binHandle), ACL_SUCCESS);
}

TEST_F(BinaryLoadFromFileTest, input_nullptr_and_mock_prof_then_test_call_expect_ok)
{
    HijackedFuncOfAclrtBinaryLoadFromFileImpl inst;
    FuncSelector::Instance().Set(ToolType::PROF);
    ASSERT_EQ(inst.Call(nullptr, nullptr, nullptr), ACL_SUCCESS);
}
