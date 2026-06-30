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


#define private public
#include "KernelRunner.h"
#undef private

#include <string>
#include <gtest/gtest.h>

#include "RuntimeApi.h"
#include "mockcpp/mockcpp.hpp"
#include "utils/FileSystem.h"


TEST(KernelRunner, run_fail_due_to_set_device_fail)
{
    KernelRunner runner;
    KernelConfig config;

    MOCKER_CPP(&RuntimeApi::RtSetDevice)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.Run(config), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, run_fail_due_to_create_stream_fail)
{
    KernelRunner runner;
    KernelConfig config;

    MOCKER_CPP(&RuntimeApi::RtSetDevice)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtStreamCreate)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.Run(config), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, run_success)
{
    KernelRunner runner;
    KernelConfig config;
    MOCKER_CPP(&RuntimeApi::RtSetDevice)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtStreamCreate)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(GetFileSize)
        .stubs()
        .will(returnValue(64U)); // 返回64个字节
    MOCKER_CPP(ReadBinary)
        .stubs()
        .will(returnValue(64U)); // 返回64个字节
    MOCKER_CPP(&KernelRunner::RegisterKernel)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&KernelRunner::InitData).stubs().will(returnValue(true));
    MOCKER_CPP(&RuntimeApi::RtKernelLaunch)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtStreamSynchronize)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&KernelRunner::SaveOutputs)
        .stubs()
        .will(returnValue(true));

    ASSERT_EQ(runner.Run(config), true);
    GlobalMockObject::verify();
}

TEST(KernelRunner, run_fail_due_to_read_binary_fail)
{
    KernelRunner runner;
    KernelConfig config;

    MOCKER_CPP(&RuntimeApi::RtSetDevice)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtStreamCreate)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(GetFileSize)
        .stubs()
        .will(returnValue(64U)); // 返回64个字节
    MOCKER_CPP(ReadBinary)
        .stubs()
        .will(returnValue(0U));

    ASSERT_EQ(runner.Run(config), false);
    GlobalMockObject::verify();
}
TEST(KernelRunner, run_fail_due_to_register_kernel_fail)
{
    KernelRunner runner;
    KernelConfig config;

    MOCKER_CPP(&RuntimeApi::RtSetDevice)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtStreamCreate)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(GetFileSize)
        .stubs()
        .will(returnValue(64U)); // 返回64个字节
    MOCKER_CPP(ReadBinary)
        .stubs()
        .will(returnValue(64U)); // 返回64个字节
    MOCKER_CPP(&KernelRunner::RegisterKernel)
        .stubs()
        .will(returnValue(false));

    ASSERT_EQ(runner.Run(config), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, run_fail_due_to_init_data_fail)
{
    KernelRunner runner;
    KernelConfig config;

    MOCKER_CPP(&RuntimeApi::RtSetDevice)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtStreamCreate)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(GetFileSize)
        .stubs()
        .will(returnValue(64U)); // 返回64个字节
    MOCKER_CPP(ReadBinary)
        .stubs()
        .will(returnValue(64U)); // 返回64个字节
    MOCKER_CPP(&KernelRunner::RegisterKernel)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&KernelRunner::InitData).stubs().will(returnValue(false));

    ASSERT_EQ(runner.Run(config), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, run_fail_due_to_kernel_launch_fail)
{
    KernelRunner runner;
    KernelConfig config;

    MOCKER_CPP(&RuntimeApi::RtSetDevice)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtStreamCreate)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(GetFileSize)
        .stubs()
        .will(returnValue(64U)); // 返回64个字节
    MOCKER_CPP(ReadBinary)
        .stubs()
        .will(returnValue(64U)); // 返回64个字节
    MOCKER_CPP(&KernelRunner::RegisterKernel)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&KernelRunner::InitData).stubs().will(returnValue(true));
    MOCKER_CPP(&RuntimeApi::RtKernelLaunch)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.Run(config), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, run_fail_due_to_sync_stream_fail)
{
    KernelRunner runner;
    KernelConfig config;

    MOCKER_CPP(&RuntimeApi::RtSetDevice)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtStreamCreate)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(GetFileSize)
        .stubs()
        .will(returnValue(64U)); // 返回64个字节
    MOCKER_CPP(ReadBinary)
        .stubs()
        .will(returnValue(64U)); // 返回64个字节
    MOCKER_CPP(&KernelRunner::RegisterKernel)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&KernelRunner::InitData).stubs().will(returnValue(true));
    MOCKER_CPP(&RuntimeApi::RtKernelLaunch)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtStreamSynchronize)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.Run(config), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, run_fail_due_to_save_output_fail)
{
    KernelRunner runner;
    KernelConfig config;

    MOCKER_CPP(&RuntimeApi::RtSetDevice)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtStreamCreate)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(GetFileSize)
        .stubs()
        .will(returnValue(64U)); // 返回64个字节
    MOCKER_CPP(ReadBinary)
        .stubs()
        .will(returnValue(64U)); // 返回64个字节
    MOCKER_CPP(&KernelRunner::RegisterKernel)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&KernelRunner::InitData).stubs().will(returnValue(true));
    MOCKER_CPP(&RuntimeApi::RtKernelLaunch)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtStreamSynchronize)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));
    MOCKER_CPP(&KernelRunner::SaveOutputs)
        .stubs()
        .will(returnValue(false));

    ASSERT_EQ(runner.Run(config), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, register_kernel_success)
{
    KernelRunner runner;
    KernelConfig config;
    uint64_t fileSize = 64U; // 模拟64字节的文件大小
    std::vector<char> data(fileSize, 0);

    MOCKER_CPP(&RuntimeApi::RtDevBinaryRegister)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtFunctionRegister)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));

    ASSERT_EQ(runner.RegisterKernel(config, data, fileSize), true);
    GlobalMockObject::verify();
}

TEST(KernelRunner, register_kernel_fail_due_to_dev_binary_register_fail)
{
    KernelRunner runner;
    KernelConfig config;
    uint64_t fileSize = 64U; // 模拟64字节的文件大小
    std::vector<char> data(fileSize, 0);

    MOCKER_CPP(&RuntimeApi::RtDevBinaryRegister)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.RegisterKernel(config, data, fileSize), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, register_kernel_fail_due_to_function_register_fail)
{
    KernelRunner runner;
    KernelConfig config;
    uint64_t fileSize = 64U; // 模拟64字节的文件大小
    std::vector<char> data(fileSize, 0);

    MOCKER_CPP(&RuntimeApi::RtDevBinaryRegister)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtFunctionRegister)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.RegisterKernel(config, data, fileSize), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_input_not_required_success)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.isRequired = false;

    ASSERT_EQ(runner.InitInput(param), true);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_input_required_success)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.isRequired = true;
    param.dataSize = 64U; // 模拟64字节的数据大小

    MOCKER_CPP(&ReadFile)
        .stubs()
        .will(returnValue(true));

    ASSERT_EQ(runner.InitInput(param), true);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_input_required_fail_due_to_read_file_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.isRequired = true;
    param.dataSize = 64U; // 模拟64字节的数据大小

    MOCKER_CPP(&ReadFile)
        .stubs()
        .will(returnValue(false));

    ASSERT_EQ(runner.InitInput(param), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_input_required_fail_due_to_rt_malloc_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.isRequired = true;
    param.dataSize = 64U; // 模拟64字节的数据大小

    MOCKER_CPP(&ReadFile)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&RuntimeApi::RtMalloc)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitInput(param), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_input_required_fail_due_to_rt_memcpy_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.isRequired = true;
    param.dataSize = 64U; // 模拟64字节的数据大小

    MOCKER_CPP(&ReadFile)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&RuntimeApi::RtMemcpy)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitInput(param), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_input_required_fail_due_to_rt_malloc_host_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.isRequired = true;
    param.dataSize = 64U; // 模拟64字节的数据大小

    MOCKER_CPP(&RuntimeApi::RtMallocHost)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitInput(param), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_output_success)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.dataSize = 64U; // 模拟64字节的数据大小

    ASSERT_EQ(runner.InitOutput(param), true);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_output_fail_due_to_rt_malloc_host_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.dataSize = 64U; // 模拟64字节的数据大小

    MOCKER_CPP(&RuntimeApi::RtMallocHost)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitOutput(param), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_workspace_success)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.dataSize = 64U; // 模拟64字节的数据大小

    ASSERT_EQ(runner.InitWorkspace(param), true);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_workspace_fail_due_to_rt_malloc_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.dataSize = 64U; // 模拟64字节的数据大小

    MOCKER_CPP(&RuntimeApi::RtMalloc)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitWorkspace(param), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_ffts_addr_success)
{
    KernelRunner runner;

    ASSERT_EQ(runner.InitFftsAddr(), true);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_ffts_addr_with_param_fail_due_to_rt_free_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.dataSize = 64U; // 模拟64字节大小

    MOCKER_CPP(&RuntimeApi::RtFree)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitFftsAddr(param), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_ffts_addr_fail_due_to_rt_get_c2c_ctrl_addr_fail)
{
    KernelRunner runner;

    MOCKER_CPP(&RuntimeApi::RtGetC2cCtrlAddr)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitFftsAddr(), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_ffts_addr_with_param_fail_due_to_rt_get_c2c_ctrl_addr_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;

    MOCKER_CPP(&RuntimeApi::RtGetC2cCtrlAddr)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitFftsAddr(param), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_ffts_addr_with_param_fail_due_to_rt_malloc_host_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;

    MOCKER_CPP(&RuntimeApi::RtGetC2cCtrlAddr)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtMallocHost)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitFftsAddr(param), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_ffts_addr_with_param_fail_due_to_rt_malloc_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;

    MOCKER_CPP(&RuntimeApi::RtGetC2cCtrlAddr)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtMallocHost)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtMalloc)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitFftsAddr(param), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_ffts_addr_with_param_fail_due_to_rt_memcpy_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.dataSize = 64U; // 模拟64字节大小

    MOCKER_CPP(&RuntimeApi::RtMemcpy)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitFftsAddr(param), false);
    runner.kernelArgs_.clear();
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_tiling_success)
{
    KernelRunner runner;
    KernelConfig::Param param;

    MOCKER_CPP(&RuntimeApi::RtMallocHost)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&ReadFile)
        .stubs()
        .will(returnValue(true)); // 返回64字节大小
    MOCKER_CPP(&RuntimeApi::RtMalloc)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtMemcpy)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));

    ASSERT_EQ(runner.InitTiling(param), true);
    runner.hostInputPtrs_.clear();
    runner.devInputPtrs_.clear();
    runner.kernelArgs_.clear();
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_tiling_fail_due_to_rt_malloc_host_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;

    MOCKER_CPP(&RuntimeApi::RtMallocHost)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitTiling(param), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_tiling_fail_due_to_read_file_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;

    MOCKER_CPP(&RuntimeApi::RtMallocHost)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&ReadFile)
        .stubs()
        .will(returnValue(false));

    ASSERT_EQ(runner.InitTiling(param), false);
    runner.hostInputPtrs_.clear();
    runner.devInputPtrs_.clear();
    runner.kernelArgs_.clear();
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_tiling_fail_due_to_rt_malloc_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;

    MOCKER_CPP(&RuntimeApi::RtMallocHost)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&ReadFile)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&RuntimeApi::RtMalloc)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitTiling(param), false);
    runner.hostInputPtrs_.clear();
    runner.devInputPtrs_.clear();
    runner.kernelArgs_.clear();
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_tiling_fail_due_to_rt_memcpy_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;

    MOCKER_CPP(&RuntimeApi::RtMallocHost)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&ReadFile)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&RuntimeApi::RtMalloc)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&RuntimeApi::RtMemcpy)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.InitTiling(param), false);
    runner.hostInputPtrs_.clear();
    runner.devInputPtrs_.clear();
    runner.kernelArgs_.clear();
    GlobalMockObject::verify();
}

TEST(KernelRunner, save_output_success)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.dataSize = 64U;
    std::string outputDataPath = "./output";
    runner.hostOutputPtrs_.emplace_back(nullptr);
    runner.outputs_.emplace_back(param);
    runner.devOutputPtrs_.emplace_back(nullptr);

    MOCKER_CPP(&RuntimeApi::RtMemcpy)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(MkdirRecusively)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(WriteBinary)
        .stubs()
        .will(returnValue(param.dataSize));

    ASSERT_EQ(runner.SaveOutputs(outputDataPath), true);
    GlobalMockObject::verify();
}

TEST(KernelRunner, save_output_fail_due_to_rt_memcpy_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.dataSize = 64U;
    std::string outputDataPath = "./output";
    runner.hostOutputPtrs_.emplace_back(nullptr);
    runner.outputs_.emplace_back(param);
    runner.devOutputPtrs_.emplace_back(nullptr);

    MOCKER_CPP(&RuntimeApi::RtMemcpy)
        .stubs()
        .will(returnValue(RT_ERROR_INVALID_VALUE));

    ASSERT_EQ(runner.SaveOutputs(outputDataPath), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, save_output_fail_due_to_mkdir_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.dataSize = 64U;
    std::string outputDataPath = "./output";
    runner.hostOutputPtrs_.emplace_back(nullptr);
    runner.outputs_.emplace_back(param);
    runner.devOutputPtrs_.emplace_back(nullptr);

    MOCKER_CPP(&RuntimeApi::RtMemcpy)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(MkdirRecusively)
        .stubs()
        .will(returnValue(false));

    ASSERT_EQ(runner.SaveOutputs(outputDataPath), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, save_output_fail_due_to_write_binary_fail)
{
    KernelRunner runner;
    KernelConfig::Param param;
    param.dataSize = 64U;
    std::string outputDataPath = "./output";
    runner.hostOutputPtrs_.emplace_back(nullptr);
    runner.outputs_.emplace_back(param);
    runner.devOutputPtrs_.emplace_back(nullptr);

    MOCKER_CPP(&RuntimeApi::RtMemcpy)
        .stubs()
        .will(returnValue(RT_ERROR_NONE));
    MOCKER_CPP(&MkdirRecusively)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&WriteBinary)
        .stubs()
        .will(returnValue(0U));

    ASSERT_EQ(runner.SaveOutputs(outputDataPath), false);
    GlobalMockObject::verify();
}

TEST(KernelRunner, init_data_fail_due_to_all_func_fail)
{
    KernelRunner runner;
    KernelConfig config;
    KernelConfig::Param param;
    config.params.emplace_back(param);

    config.params[0].type = "input";
    MOCKER_CPP(&KernelRunner::InitInput)
        .stubs()
        .will(returnValue(false));
    ASSERT_EQ(runner.InitData(config), false);

    config.params[0].type = "output";
    MOCKER_CPP(&KernelRunner::InitOutput)
        .stubs()
        .will(returnValue(false));
    ASSERT_EQ(runner.InitData(config), false);

    config.params[0].type = "tiling";
    MOCKER_CPP(&KernelRunner::InitTiling)
        .stubs()
        .will(returnValue(false));
    ASSERT_EQ(runner.InitData(config), false);

    config.params[0].type = "workspace";
    MOCKER_CPP(&KernelRunner::InitWorkspace)
        .stubs()
        .will(returnValue(false));
    ASSERT_EQ(runner.InitData(config), false);

    config.params[0].type = "fftsAddr";
    config.hasTilingKey = true;
    MOCKER_CPP(&KernelRunner::InitFftsAddr, bool(KernelRunner::*)())
        .stubs()
        .will(returnValue(false));
    ASSERT_EQ(runner.InitData(config), false);

    config.params[0].type = "fftsAddr";
    config.hasTilingKey = false;
    MOCKER_CPP(&KernelRunner::InitFftsAddr, bool(KernelRunner::*)(const KernelConfig::Param &))
        .stubs()
        .will(returnValue(false));
    ASSERT_EQ(runner.InitData(config), false);

    GlobalMockObject::verify();
}

TEST(KernelRunner, init_data_success_output)
{
    KernelRunner runner;
    KernelConfig config;
    KernelConfig::Param param;
    config.params.emplace_back(param);

    config.params[0].type = "output";
    MOCKER_CPP(&KernelRunner::InitOutput)
        .stubs()
        .will(returnValue(true));
    ASSERT_EQ(runner.InitData(config), true);

    GlobalMockObject::verify();
}
