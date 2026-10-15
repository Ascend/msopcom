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
#include "runtime/inject_helpers/RegisterManager.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "utils/FileSystem.h"

using namespace std;

class RegisterManagerTest : public testing::Test {
public:
    void SetUp() override {}

    void TearDown() override
    {
        GlobalMockObject::verify();
        RegisterManager::Instance().Clear();
    }
};

static bool GetValidNameFromBinary(const char *data, uint64_t length, std::vector<std::string> &kernelNames,
    std::vector<uint64_t> &kernelOffsets, bool &hasSimt) {
    hasSimt = false;
    kernelNames.emplace_back("valid_kernel_1234_mix_aic");
    kernelOffsets.emplace_back(0);
    return true;
}

TEST_F(RegisterManagerTest, mock_sym_info_then_test_create_with_file_path_expect_get_success)
{
    MOCKER(&ScanKernelSymbols).stubs().will(invoke(GetValidNameFromBinary));
    MOCKER(&HasStaticStub).stubs().will(returnValue(true));
    const char *path = "empty.o";
    uint32_t data{};
    vector<char> elfData(100, 1);
    aclrtBinHandle binHandle = &data;
    MOCKER_CPP(&ReadBinary).stubs().with(any(), outBound(elfData)).will(returnValue(size_t(1)));
    auto ctx = RegisterManager::Instance().CreateContext(path, binHandle, RT_DEV_BINARY_MAGIC_ELF);
    ASSERT_NE(ctx, nullptr);
    auto gotCtx = RegisterManager::Instance().GetContext(binHandle);
    ASSERT_EQ(ctx, gotCtx);
}

TEST_F(RegisterManagerTest, mock_register_context_then_test_create_with_file_path_expect_get_success)
{
    MOCKER(&ScanKernelSymbols).stubs().will(invoke(GetValidNameFromBinary));
    MOCKER(&HasStaticStub).stubs().will(returnValue(true));

    vector<char> elfData(100, 1);
    aclrtBinary bin = elfData.data();
    RegisterManager::Instance().CacheElfData(bin, elfData.data(), elfData.size());

    aclrtBinHandle binHandle = elfData.data();
    auto ctx = RegisterManager::Instance().CreateContext(bin, binHandle, RT_DEV_BINARY_MAGIC_ELF);
    ASSERT_NE(ctx, nullptr);
    auto gotCtx = RegisterManager::Instance().GetContext(binHandle);
    ASSERT_EQ(ctx, gotCtx);
}

TEST_F(RegisterManagerTest, mock_register_context_then_test_create_from_data_expect_get_success)
{
    MOCKER(&ScanKernelSymbols).stubs().will(invoke(GetValidNameFromBinary));
    MOCKER(&HasStaticStub).stubs().will(returnValue(true));

    vector<char> elfData(100, 1);
    aclrtBinHandle binHandle = elfData.data();
    const void *data = static_cast<const void*>(elfData.data());
    auto ctx = RegisterManager::Instance().CreateContext(data, elfData.size(), binHandle, RT_DEV_BINARY_MAGIC_ELF, nullptr);
    ASSERT_NE(ctx, nullptr);
    auto gotCtx = RegisterManager::Instance().GetContext(binHandle);
    ASSERT_EQ(ctx, gotCtx);
}
