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
#include <vector>
#include "mockcpp/mockcpp.hpp"
#include "acl_rt_impl/HijackedFunc.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/RegisterManager.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ProfDataCollect.h"

class BinaryLoadFromDataTest : public testing::Test {
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

TEST_F(BinaryLoadFromDataTest, fake_input_and_mock_prof_then_test_call_expect_ok)
{
    HijackedFuncOfAclrtBinaryLoadFromDataImpl inst;
    auto sp = std::make_shared<RegisterContext>();
    MOCKER_CPP(&RegisterManager::CreateContext,
               RegisterContextSP(RegisterManager::*)(const void*, size_t,
                   aclrtBinHandle, uint32_t, aclrtBinaryLoadOptions *)).stubs().will(returnValue(sp));
    MOCKER_CPP(&ProfConfig::IsSimulator).stubs().will(returnValue(true));
    MOCKER_CPP(&ProfDataCollect::SaveObject, bool(*)(const RegisterContextSP &)).stubs().will(returnValue(true));

    FuncSelector::Instance().Set(ToolType::PROF);
    setenv(DEVICE_TO_SIMULATOR, "true", 1);
    std::vector<char> elfData(100, 1);
    const void *data = static_cast<const void *>(elfData.data());
    aclrtBinaryLoadOptions options{};
    aclrtBinHandle binHandle;
    ASSERT_EQ(inst.Call(data, elfData.size(), &options, &binHandle), ACL_SUCCESS);
}

TEST_F(BinaryLoadFromDataTest, input_nullptr_and_mock_prof_then_test_call_expect_ok)
{
    HijackedFuncOfAclrtBinaryLoadFromDataImpl inst;
    FuncSelector::Instance().Set(ToolType::PROF);
    ASSERT_EQ(inst.Call(nullptr, 0, nullptr, nullptr), ACL_SUCCESS);
}
