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

#include "mockcpp/mockcpp.hpp"
#define private public
#include "Launcher.h"
#undef private

#include <string>
#include <gtest/gtest.h>

#include "acl.h"
#include "utils/FileSystem.h"
class LauncherTest : public testing::Test {
public:
    void SetUp() override { }

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(LauncherTest, run_fail_due_to_set_device_fail)
{
    Launcher runner;
    KernelConfig config;
    MOCKER_CPP(&aclrtSetDevice).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    ASSERT_EQ(runner.Run(config), false);
}

TEST_F(LauncherTest, run_fail_due_to_create_stream_fail)
{
    Launcher runner;
    KernelConfig config;
    MOCKER_CPP(&aclrtSetDevice).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtCreateStream).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));

    ASSERT_EQ(runner.Run(config), false);
}
TEST_F(LauncherTest, run_success)
{
    Launcher runner;
    KernelConfig config;
    MOCKER_CPP(&aclrtSetDevice).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtCreateStream).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&Launcher::RegisterKernel).stubs().will(returnValue(true));
    MOCKER_CPP(&Launcher::InitData).stubs().will(returnValue(true));
    MOCKER_CPP(&aclrtBinaryLoadFromFile).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtBinaryGetFunction).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtKernelArgsInit).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtKernelArgsAppend).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtKernelArgsFinalize).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtLaunchKernelWithConfig).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtSynchronizeStream).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&Launcher::SaveOutputs).stubs().will(returnValue(true));
    ASSERT_EQ(runner.Run(config), true);
}
TEST_F(LauncherTest, run_fail_due_to_register_kernel_fail)
{
    Launcher runner;
    KernelConfig config;
    MOCKER_CPP(&aclrtSetDevice).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtCreateStream).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&Launcher::RegisterKernel).stubs().will(returnValue(false));
    ASSERT_EQ(runner.Run(config), false);
}
TEST_F(LauncherTest, run_fail_due_to_init_data_fail)
{
    Launcher runner;
    KernelConfig config;
    MOCKER_CPP(&aclrtSetDevice).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtCreateStream).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&Launcher::RegisterKernel).stubs().will(returnValue(true));
    MOCKER_CPP(&Launcher::InitData).stubs().will(returnValue(false));

    ASSERT_EQ(runner.Run(config), false);
}

TEST_F(LauncherTest, run_fail_due_to_kernel_launch_fail)
{
    Launcher runner;
    KernelConfig config;

    MOCKER_CPP(&aclrtSetDevice).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtCreateStream).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&Launcher::RegisterKernel).stubs().will(returnValue(true));
    MOCKER_CPP(&Launcher::InitData).stubs().will(returnValue(true));
    MOCKER_CPP(&aclrtKernelArgsAppend).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtKernelArgsFinalize).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtLaunchKernelWithConfig).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));

    ASSERT_EQ(runner.Run(config), false);
}

TEST_F(LauncherTest, run_fail_due_to_sync_stream_fail)
{
    Launcher runner;
    KernelConfig config;

    MOCKER_CPP(&aclrtSetDevice).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtCreateStream).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&Launcher::RegisterKernel).stubs().will(returnValue(true));
    MOCKER_CPP(&Launcher::InitData).stubs().will(returnValue(true));
    MOCKER_CPP(&Launcher::LaunchKernel).stubs().will(returnValue(true));
    MOCKER_CPP(&aclrtSynchronizeStream).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));

    ASSERT_EQ(runner.Run(config), false);
}
TEST_F(LauncherTest, run_fail_due_to_save_output_fail)
{
    Launcher runner;
    KernelConfig config;

    MOCKER_CPP(&aclrtSetDevice).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtCreateStream).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&Launcher::RegisterKernel).stubs().will(returnValue(true));
    MOCKER_CPP(&Launcher::InitData).stubs().will(returnValue(true));
    MOCKER_CPP(&aclrtLaunchKernelWithConfig).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtSynchronizeStream).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&Launcher::SaveOutputs).stubs().will(returnValue(false));

    ASSERT_EQ(runner.Run(config), false);
}

TEST_F(LauncherTest, register_kernel_success)
{
    Launcher runner;
    KernelConfig configwithTilingkey;
    KernelConfig configwithName;
    uint64_t fileSize = 64U; // 模拟64字节的文件大小
    std::vector<char> data(fileSize, 0);

    MOCKER_CPP(&aclrtBinaryLoadFromFile).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtBinaryGetFunctionByEntry).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtBinaryGetFunction).stubs().will(returnValue(ACL_SUCCESS));
    configwithTilingkey.hasTilingKey = true;

    ASSERT_EQ(runner.RegisterKernel(configwithTilingkey, ""), true);
    ASSERT_EQ(runner.RegisterKernel(configwithName, ""), true);
}

TEST_F(LauncherTest, register_kernel_fail_due_to_dev_binary_register_fail)
{
    Launcher runner;
    KernelConfig config;
    uint64_t fileSize = 64U; // 模拟64字节的文件大小
    std::vector<char> data(fileSize, 0);

    MOCKER_CPP(&aclrtBinaryLoadFromFile).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    ASSERT_EQ(runner.RegisterKernel(config, ""), false);
}

TEST_F(LauncherTest, register_kernel_fail_due_to_function_register_fail)
{
    Launcher runner;
    KernelConfig configwithTilingkey;
    KernelConfig configwithName;
    uint64_t fileSize = 64U; // 模拟64字节的文件大小
    std::vector<char> data(fileSize, 0);
    MOCKER_CPP(&aclrtBinaryLoadFromFile).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtBinaryGetFunctionByEntry).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    MOCKER_CPP(&aclrtBinaryGetFunction).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    configwithTilingkey.hasTilingKey = true;

    ASSERT_EQ(runner.RegisterKernel(configwithTilingkey, ""), false);
    ASSERT_EQ(runner.RegisterKernel(configwithName, ""), false);
}

TEST_F(LauncherTest, init_input_not_required_success)
{
    Launcher runner;
    KernelConfig::Param param;
    param.isRequired = false;

    ASSERT_EQ(runner.InitInput(param), true);
}

TEST_F(LauncherTest, init_input_required_success)
{
    Launcher runner;
    KernelConfig::Param param;
    param.isRequired = true;
    param.dataSize = 64U; // 模拟64字节的数据大小

    MOCKER_CPP(&ReadFile).stubs().will(returnValue(true));

    ASSERT_EQ(runner.InitInput(param), true);
}

TEST_F(LauncherTest, init_input_required_fail_due_to_read_file_fail)
{
    Launcher runner;
    KernelConfig::Param param;
    param.isRequired = true;
    param.dataSize = 64U; // 模拟64字节的数据大小

    MOCKER_CPP(&ReadFile).stubs().will(returnValue(false));

    ASSERT_EQ(runner.InitInput(param), false);
}

TEST_F(LauncherTest, init_input_required_fail_due_to_rt_malloc_host_fail)
{
    Launcher runner;
    KernelConfig::Param param;
    param.isRequired = true;
    param.dataSize = 64U; // 模拟64字节的数据大小

    MOCKER_CPP(&ReadFile).stubs().will(returnValue(true));
    MOCKER_CPP(&aclrtMallocHost).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));

    ASSERT_EQ(runner.InitInput(param), false);
}

TEST_F(LauncherTest, init_input_required_fail_due_to_rt_malloc_fail)
{
    Launcher runner;
    KernelConfig::Param param;
    param.isRequired = true;
    param.dataSize = 64U; // 模拟64字节的数据大小

    MOCKER_CPP(&ReadFile).stubs().will(returnValue(true));
    MOCKER_CPP(&aclrtMallocHost).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtMalloc).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));

    ASSERT_EQ(runner.InitInput(param), false);
}


TEST_F(LauncherTest, init_input_required_fail_due_to_rt_memcpy_fail)
{
    Launcher runner;
    KernelConfig::Param param;
    param.isRequired = true;
    param.dataSize = 64U; // 模拟64字节的数据大小

    MOCKER_CPP(&ReadFile).stubs().will(returnValue(true));
    MOCKER_CPP(&aclrtMallocHost).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtMalloc).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtMemcpy).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));

    ASSERT_EQ(runner.InitInput(param), false);
}

TEST_F(LauncherTest, init_output_success)
{
    Launcher runner;
    KernelConfig::Param param;
    param.dataSize = 64U; // 模拟64字节的数据大小
    MOCKER_CPP(&aclrtMallocHost).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtMalloc).stubs().will(returnValue(ACL_SUCCESS));
    ASSERT_EQ(runner.InitOutput(param), true);
}

TEST_F(LauncherTest, init_output_fail_due_to_rt_malloc_host_fail)
{
    Launcher runner;
    KernelConfig::Param param;
    param.dataSize = 64U; // 模拟64字节的数据大小
    MOCKER_CPP(&aclrtMallocHost).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    ASSERT_EQ(runner.InitOutput(param), false);
}

TEST_F(LauncherTest, init_output_fail_due_to_rt_malloc_fail)
{
    Launcher runner;
    KernelConfig::Param param;
    param.dataSize = 64U; // 模拟64字节的数据大小
    MOCKER_CPP(&aclrtMallocHost).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtMalloc).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    ASSERT_EQ(runner.InitOutput(param), false);
}

TEST_F(LauncherTest, init_workspace_success)
{
    Launcher runner;
    KernelConfig::Param param;
    param.dataSize = 64U; // 模拟64字节的数据大小
    MOCKER_CPP(&aclrtMalloc).stubs().will(returnValue(ACL_SUCCESS));
    ASSERT_EQ(runner.InitWorkspace(param), true);
}

TEST_F(LauncherTest, init_workspace_failed)
{
    Launcher runner;
    KernelConfig::Param param;
    param.dataSize = 64U; // 模拟64字节的数据大小
    MOCKER_CPP(&aclrtMalloc).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    ASSERT_EQ(runner.InitWorkspace(param), false);
}

TEST_F(LauncherTest, init_tiling_success)
{
    Launcher runner;
    KernelConfig::Param param;
    MOCKER_CPP(&aclrtMallocHost).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&ReadFile).stubs().will(returnValue(true)); // 返回64字节大小
    MOCKER_CPP(&aclrtMalloc).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtMemcpy).stubs().will(returnValue(ACL_SUCCESS));
    ASSERT_EQ(runner.InitTiling(param), true);
    runner.hostInputPtrs_.clear();
    runner.devInputPtrs_.clear();
    runner.kernelArgs_.clear();
}

TEST_F(LauncherTest, init_tiling_fail_due_to_rt_malloc_host_fail)
{
    Launcher runner;
    KernelConfig::Param param;
    MOCKER_CPP(&aclrtMallocHost).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    ASSERT_EQ(runner.InitTiling(param), false);
}

TEST_F(LauncherTest, init_tiling_fail_due_to_read_file_fail)
{
    Launcher runner;
    KernelConfig::Param param;
    MOCKER_CPP(&aclrtMallocHost).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&ReadFile).stubs().will(returnValue(false));
    ASSERT_EQ(runner.InitTiling(param), false);
    runner.hostInputPtrs_.clear();
    runner.devInputPtrs_.clear();
    runner.kernelArgs_.clear();
}

TEST_F(LauncherTest, init_tiling_fail_due_to_rt_malloc_fail)
{
    Launcher runner;
    KernelConfig::Param param;
    MOCKER_CPP(&aclrtMallocHost).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&ReadFile).stubs().will(returnValue(true));
    MOCKER_CPP(&aclrtMalloc).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    ASSERT_EQ(runner.InitTiling(param), false);
    runner.hostInputPtrs_.clear();
    runner.devInputPtrs_.clear();
    runner.kernelArgs_.clear();
}

TEST_F(LauncherTest, init_tiling_fail_due_to_rt_memcpy_fail)
{
    Launcher runner;
    KernelConfig::Param param;
    MOCKER_CPP(&aclrtMallocHost).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&ReadFile).stubs().will(returnValue(true));
    MOCKER_CPP(&aclrtMalloc).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&aclrtMemcpy).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    ASSERT_EQ(runner.InitTiling(param), false);
    runner.hostInputPtrs_.clear();
    runner.devInputPtrs_.clear();
    runner.kernelArgs_.clear();
}

TEST_F(LauncherTest, save_output_success)
{
    Launcher runner;
    KernelConfig::Param param;
    param.dataSize = 64U;
    std::string outputDataPath = "./output";
    RemoveAll(outputDataPath);
    ASSERT_TRUE(MkdirRecusively(outputDataPath));
    runner.hostOutputPtrs_.emplace_back(nullptr);
    runner.outputs_.emplace_back(param);
    runner.devOutputPtrs_.emplace_back(nullptr);
    MOCKER_CPP(&aclrtMemcpy).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(MkdirRecusively).stubs().will(returnValue(true));
    MOCKER_CPP(WriteBinary).stubs().will(returnValue(param.dataSize));
    ASSERT_EQ(runner.SaveOutputs(outputDataPath), true);
    RemoveAll(outputDataPath);
}

TEST_F(LauncherTest, save_output_fail_due_to_rt_memcpy_fail)
{
    Launcher runner;
    KernelConfig::Param param;
    param.dataSize = 64U;
    std::string outputDataPath = "./output";
    RemoveAll(outputDataPath);
    ASSERT_TRUE(MkdirRecusively(outputDataPath));
    runner.hostOutputPtrs_.emplace_back(nullptr);
    runner.outputs_.emplace_back(param);
    runner.devOutputPtrs_.emplace_back(nullptr);
    MOCKER_CPP(&aclrtMemcpy).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    ASSERT_EQ(runner.SaveOutputs(outputDataPath), false);
}

TEST_F(LauncherTest, save_output_fail_due_to_mkdir_fail)
{
    Launcher runner;
    KernelConfig::Param param;
    param.dataSize = 64U;
    std::string outputDataPath = "./output";
    runner.hostOutputPtrs_.emplace_back(nullptr);
    runner.outputs_.emplace_back(param);
    runner.devOutputPtrs_.emplace_back(nullptr);
    MOCKER_CPP(&aclrtMemcpy).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(MkdirRecusively).stubs().will(returnValue(false));
    ASSERT_EQ(runner.SaveOutputs(outputDataPath), false);
}

TEST_F(LauncherTest, save_output_fail_due_to_write_binary_fail)
{
    Launcher runner;
    KernelConfig::Param param;
    param.dataSize = 64U;
    std::string outputDataPath = "./output";
    runner.hostOutputPtrs_.emplace_back(nullptr);
    runner.outputs_.emplace_back(param);
    runner.devOutputPtrs_.emplace_back(nullptr);
    MOCKER_CPP(&aclrtMemcpy).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER_CPP(&MkdirRecusively).stubs().will(returnValue(true));
    MOCKER_CPP(&WriteBinary).stubs().will(returnValue(0U));
    ASSERT_EQ(runner.SaveOutputs(outputDataPath), false);
    RemoveAll(outputDataPath);
}

TEST_F(LauncherTest, init_data_success_output)
{
    Launcher runner;
    KernelConfig config;
    KernelConfig::Param param;
    config.params.emplace_back(param);
    config.params[0].type = "output";
    MOCKER_CPP(&Launcher::InitOutput).stubs().will(returnValue(true));
    ASSERT_EQ(runner.InitData(config), true);
}
