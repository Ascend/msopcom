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

#include "acl_rt_impl/HijackedFunc.h"
#include "runtime/inject_helpers/FuncManager.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <fstream>
#include <vector>

#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/LaunchContext.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "utils/FileSystem.h"
#include "utils/ElfLoader.h"
#include "ContextMockHelper.h"

using namespace std;

class LaunchContextTest : public ContextMockHelper {
};

namespace {
class SavingArgsContext : public ArgsContext {
public:
    bool ExpandArgs(void *, size_t, uint32_t &) override { return false; }
    bool Save(const std::string &, DumperContext &config, OpMemInfo &, bool) override
    {
        config.inputPathList = {"input0"};
        config.inputSizeList = {64};
        return true;
    }
    ArgsContextSP Clone() const override { return std::make_shared<SavingArgsContext>(); }
};
}
TEST_F(LaunchContextTest, mock_valid_input_then_test_get_context_method_expect_success)
{
    aclrtStream stream = &placeholder_;
    LaunchParam param{ 1, stream, true, 1};
    auto launchCtx = make_shared<LaunchContext>(tilingFuncCtx_, argsBinCtx_, param);
    ASSERT_NE(launchCtx, nullptr);
    ASSERT_EQ(launchCtx->GetFuncContext(), tilingFuncCtx_);
    ASSERT_EQ(launchCtx->GetArgsContext(), argsBinCtx_);
    ASSERT_EQ(launchCtx->GetLaunchId(), param.launchId);
    ASSERT_EQ(launchCtx->IsSink(), param.isSink);
}
TEST_F(LaunchContextTest, save_writes_binary_and_launch_parameters_from_the_same_context) {
    MOCKER(&ScanKernelSymbols).stubs().will(returnValue(true));
    MOCKER(&ElfLoader::LoadHeader).stubs().will(returnValue(true));
    RegisterParam regParam(0, RT_DEV_BINARY_MAGIC_ELF_AICUBE, nullptr, false, true);
    ASSERT_TRUE(regCtx_->Init(std::vector<char>(128, 0), binHandle_, regParam));
    LaunchManager::Local().GetCurrentMemInfo().isFFTS = true;
    LaunchManager::Local().ArchiveMemInfo();
    LaunchParam param{8, nullptr, false, 0};
    LaunchContext launchCtx(tilingFuncCtx_, std::make_shared<SavingArgsContext>(), param);
    EXPECT_EQ(launchCtx.GetDBIFuncCtx(), nullptr);
    auto dbiFunc = std::make_shared<TilingFuncContext>(regCtx_, 0, funcHandle_);
    launchCtx.SetDBIFuncCtx(dbiFunc);
    EXPECT_EQ(launchCtx.GetDBIFuncCtx(), dbiFunc);
    dbiFunc.reset();
    EXPECT_EQ(launchCtx.GetDBIFuncCtx(), nullptr);
    const std::string outputDir = "./launch_context_save_test";
    ASSERT_TRUE(MkdirRecusively(outputDir));
    ASSERT_TRUE(launchCtx.Save(outputDir));
    std::vector<char> binary;
    EXPECT_EQ(ReadBinary(JoinPath({outputDir, KERNEL_BIN_NAME}), binary), 128U);
    std::ifstream file(JoinPath({outputDir, KERNEL_CONFIG_NAME}));
    ASSERT_TRUE(file.is_open());
    nlohmann::json json = nlohmann::json::parse(file);
    EXPECT_EQ(json.at("block_dim"), "8");
    EXPECT_EQ(json.at("ffts"), "Y");
    EXPECT_EQ(json.at("input_path"), "input0");
    EXPECT_EQ(json.at("input_size"), "64");
    EXPECT_EQ(json.at("magic"), "RT_DEV_BINARY_MAGIC_ELF_AICUBE");
    EXPECT_EQ(json.at("old_mode"), "0");
    file.close();
    RemoveAll(outputDir);
}
class CpuFunctionRegistrationTest : public ContextMockHelper {};

TEST_F(CpuFunctionRegistrationTest, registration_marks_only_successful_function_as_aicpu) {
    aclrtFuncHandle result = funcHandle_;
    HijackedFuncOfAclrtRegisterCpuFuncImpl hook;
    hook.Pre(binHandle_, "cpu_entry", "cpu_kernel", &result);
    EXPECT_EQ(hook.Post(ACL_ERROR_BAD_ALLOC), ACL_ERROR_BAD_ALLOC);
    EXPECT_EQ(FuncManager::Instance().GetContext(result), tilingFuncCtx_);
    EXPECT_FALSE(tilingFuncCtx_->isAiCpu);
    EXPECT_EQ(hook.Post(ACL_SUCCESS), ACL_SUCCESS);
    auto context = FuncManager::Instance().GetContext(result);
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->GetKernelName(), "cpu_entry");
    EXPECT_TRUE(context->isAiCpu);
}
