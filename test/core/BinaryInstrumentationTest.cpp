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


#include <sstream>
#include <dlfcn.h>

#include <gtest/gtest.h>
#define private public
#include "core/BinaryInstrumentation.h"
#undef private
#include "utils/PipeCall.h"
#include "mockcpp/mockcpp.hpp"
#include "utils/FileSystem.h"

using namespace std;

TEST(BBCountDBI, covert_kernel_expect_success)
{
    MOCKER(PipeCall).stubs().will(returnValue(true));
    MOCKER(chmod).stubs().will(returnValue(0));
    BBCountDBI bbcount;
    bool ret = bbcount.Convert("", "");
    bool needExtraSpace = bbcount.NeedExtraSpace();
    bool exArgs = bbcount.ExpandArgs("");
    EXPECT_TRUE(ret);
    EXPECT_TRUE(needExtraSpace);
    EXPECT_TRUE(exArgs);
    GlobalMockObject::verify();
}

TEST(BBCountDBI, covert_kernel_expect_false)
{
    MOCKER(PipeCall).stubs().will(returnValue(false));
    BBCountDBI bbcount;
    bool ret = bbcount.Convert("", "", "1");
    bool needExtraSpace = bbcount.NeedExtraSpace();
    bool exArgs = bbcount.ExpandArgs("");
    EXPECT_FALSE(ret);
    GlobalMockObject::verify();
}

TEST(PGODBI, covert_kernel_expect_success)
{
    MOCKER(PipeCall).stubs().will(returnValue(true));
    MOCKER(chmod).stubs().will(returnValue(0));
    PGODBI pgo;
    bool ret = pgo.Convert("", "");
    bool needExtraSpace = pgo.NeedExtraSpace();
    bool exArgs = pgo.ExpandArgs("");
    EXPECT_TRUE(ret);
    EXPECT_FALSE(needExtraSpace);
    EXPECT_TRUE(exArgs);
    GlobalMockObject::verify();
}

TEST(PGODBI, covert_kernel_expect_false)
{
    MOCKER(PipeCall).stubs().will(returnValue(false));
    PGODBI pgo;
    bool ret = pgo.Convert("", "", "1");
    bool needExtraSpace = pgo.NeedExtraSpace();
    bool exArgs = pgo.ExpandArgs("");
    EXPECT_FALSE(ret);
    GlobalMockObject::verify();
}

TEST(CustomDBI, input_invalid_lib_path_then_set_plugin_path_convert_expect_fail)
{
    MOCKER(PipeCall).stubs().will(returnValue(true));
    CustomDBI dbi;
    BinaryInstrumentation::Config config{};
    bool ret = dbi.SetConfig(config);
    EXPECT_FALSE(ret);
    GlobalMockObject::verify();
}

TEST(CustomDBI, input_invalid_lib_path_then_set_plugin_path_convert_expect_True)
{
    MOCKER(PipeCall).stubs().will(returnValue(false));
    CustomDBI dbi;
    BinaryInstrumentation::Config config{};
    bool ret = dbi.SetConfig(config);
    EXPECT_FALSE(ret);
    GlobalMockObject::verify();
}

void BitMockFunc(const char *output, uint16_t length)
{
    return;
}

void StubInitFunc(const char *, uint16_t, const char *) {}

TEST(CustomDBI, input_valid_lib_path_then_set_plugin_path_expect_success)
{
    {
        MOCKER(PipeCall).stubs().will(returnValue(true));
        string handle = "handle";
        void *res = &handle;
        MOCKER(&dlopen).stubs().will(returnValue(res));
        MOCKER(&dlclose).stubs().will(returnValue(int(0)));
        MOCKER(&dlsym).stubs().will(returnValue(res));
        CustomDBI dbi;
        BinaryInstrumentation::Config config{"./libmem_trace.so", "dav-c220"};
        FILE *fp = fopen(config.pluginPath.c_str(), "w");
        if (fp != nullptr) {
            fclose(fp);
        }
        bool ret = dbi.SetConfig(config);
        EXPECT_TRUE(ret);
        remove(config.pluginPath.c_str());
    }
    GlobalMockObject::verify();
}

TEST(CustomDBI, input_valid_lib_path_then_convert_expect_success)
{
    {
        MOCKER(PipeCall).stubs().will(returnValue(true));
        MOCKER(chmod).stubs().will(returnValue(0));
        string handle = "handle";
        void *res = &handle;
        MOCKER(&dlopen).stubs().will(returnValue(res));
        MOCKER(&dlsym).stubs().will(returnValue((void *)BitMockFunc));
        MOCKER(&dlclose).stubs().will(returnValue(int(0)));
        MOCKER(&CustomDBI::GenerateOrderingFile).stubs().will(returnValue(true));

        // this need be destructor to call dlopen before mock verify
        CustomDBI dbi;
        BinaryInstrumentation::Config config{"./libmem_trace.so", ".ascend910B"};
        FILE *fp = fopen(config.pluginPath.c_str(), "w");
        if (fp != nullptr) {
            fclose(fp);
        }
        bool ret = dbi.SetConfig(config);
        ret = dbi.Convert("new_kernel.o", "old_kernel.o");
        EXPECT_TRUE(ret);
        remove(config.pluginPath.c_str());
    }
    GlobalMockObject::verify();
}

TEST(CustomDBI, input_valid_lib_path_then_convert_expect_fail)
{
    {
        MOCKER(PipeCall).stubs().will(returnValue(true));
        MOCKER(chmod).stubs().will(returnValue(0));
        string handle = "handle";
        void *res = &handle;
        MOCKER(&dlopen).stubs().will(returnValue(res));
        MOCKER(&dlsym).stubs().will(returnValue((void *)BitMockFunc));
        MOCKER(&dlclose).stubs().will(returnValue(int(0)));
        MOCKER(&CustomDBI::GenerateOrderingFile).stubs().will(returnValue(true));

        // this need be destructor to call dlopen before mock verify
        CustomDBI dbi;
        BinaryInstrumentation::Config config{"./libmem_trace.so", ".ascend910B"};
        FILE *fp2 = fopen(config.pluginPath.c_str(), "w");
        if (fp2 != nullptr) {
            fclose(fp2);
        }
        bool ret = dbi.SetConfig(config);
        ret = dbi.Convert("new_kernel.o", "old_kernel.o", "1");
        EXPECT_TRUE(ret);
        remove(config.pluginPath.c_str());
    }
    GlobalMockObject::verify();
}

TEST(CustomDBI, kernel_binary_objdump_failed_expect_generate_ordering_file_return_false)
{
    MOCKER(PipeCall).stubs().will(returnValue(false));

    CustomDBI dbi;
    bool ret = dbi.GenerateOrderingFile("kernel.o", "probe.o", "ordering.txt");
    EXPECT_FALSE(ret);
    GlobalMockObject::verify();
}

bool PipeCallStubForInvalidKernelObjdump(std::vector<std::string> const &cmd, std::string &output)
{
    output = "0000000000000000\tg\tF\t.unknown\t0000000000000000\tkernel";
    return true;
}

TEST(CustomDBI, kernel_parse_first_symbol_failed_expect_generate_ordering_file_return_false)
{
    MOCKER(PipeCall).stubs().will(invoke(PipeCallStubForInvalidKernelObjdump));

    CustomDBI dbi;
    bool ret = dbi.GenerateOrderingFile("kernel.o", "probe.o", "ordering.txt");
    EXPECT_FALSE(ret);
    GlobalMockObject::verify();
}

bool PipeCallStubForProbeObjdumpFailed(std::vector<std::string> const &cmd, std::string &output)
{
    // 注入 pipecall，在对 kernel.o 进行 objdump 时返回正确的结果，对 probe.o 进行 objdump 时返回 false
    constexpr size_t fileNameIdx = 2;
    if (cmd[fileNameIdx] == "kernel.o") {
        output = "0000000000000000\tg\tF\t.text\t0000000000000000\tkernel";
        return true;
    }
    return false;
}

TEST(CustomDBI, probe_binary_objdump_failed_expect_generate_ordering_file_return_false)
{
    MOCKER(PipeCall).stubs().will(invoke(PipeCallStubForProbeObjdumpFailed));

    CustomDBI dbi;
    bool ret = dbi.GenerateOrderingFile("kernel.o", "probe.o", "ordering.txt");
    EXPECT_FALSE(ret);
    GlobalMockObject::verify();
}

bool PipeCallStubForInvalidProbeObjdump(std::vector<std::string> const &cmd, std::string &output)
{
    constexpr size_t fileNameIdx = 2;
    if (cmd[fileNameIdx] == "kernel.o") {
        output = "0000000000000000\tg\tF\t.text\t0000000000000000\tkernel";
    } else {
        output = "0000000000000000\tw\tF\t.unknown\t0000000000000000\tprobe";
    }
    return true;
}

TEST(CustomDBI, probe_parse_first_symbol_failed_expect_generate_ordering_file_return_false)
{
    MOCKER(PipeCall).stubs().will(invoke(PipeCallStubForInvalidProbeObjdump));

    CustomDBI dbi;
    bool ret = dbi.GenerateOrderingFile("kernel.o", "probe.o", "ordering.txt");
    EXPECT_FALSE(ret);
    GlobalMockObject::verify();
}

bool PipeCallStubForGetValidObjdumpResult(std::vector<std::string> const &cmd, std::string &output)
{
    constexpr size_t fileNameIdx = 2;
    if (cmd[fileNameIdx] == "kernel.o") {
        output = "0000000000000000\tg\tF\t.text\t0000000000000000\tkernel";
    } else {
        output = "0000000000000000\tw\tF\t.text\t0000000000000000\tprobe";
    }
    return true;
}

TEST(CustomDBI, kernel_and_probe_parse_symbol_success_expect_generate_ordering_file_return_true)
{
    MOCKER(PipeCall).stubs().will(invoke(PipeCallStubForGetValidObjdumpResult));

    CustomDBI dbi;
    bool ret = dbi.GenerateOrderingFile("kernel.o", "probe.o", "ordering.txt");
    EXPECT_TRUE(ret);
    GlobalMockObject::verify();
}

TEST(CustomDBI, GenerateKernelWithProbe_expect_return_false)
{
    MOCKER(PipeCall).stubs().will(returnValue(false));
    CustomDBI dbi;
    EXPECT_FALSE(dbi.GenerateKernelWithProbe("", "", "", ""));
    GlobalMockObject::verify();
}

TEST(CustomDBI, GenerateTempProbe_expect_return_false)
{
    MOCKER(PipeCall).stubs().will(returnValue(false));
    CustomDBI dbi;
    EXPECT_FALSE(dbi.GenerateTempProbe("/probe"));
    GlobalMockObject::verify();
}

TEST(CustomDBI, write_order_file_failed_return_false)
{
    MOCKER(PipeCall).stubs().will(invoke(PipeCallStubForGetValidObjdumpResult));

    CustomDBI dbi;
    bool ret = dbi.GenerateOrderingFile("kernel.o", "probe.o", "1!@#$/\\{}/");
    EXPECT_FALSE(ret);
    GlobalMockObject::verify();
}

TEST(CustomDBI, dl_interface_failed_set_config_return_false)
{
    MOCKER(dlopen).stubs().will(returnValue((void*)nullptr)).then(returnValue((void*)0x1));
    MOCKER(dlsym).stubs().will(returnValue((void*)nullptr));
    MOCKER(dlclose).stubs().will(returnValue(0));
    {
        CustomDBI dbi;
        BinaryInstrumentation::Config config{"plugin", "arch", "tmp_dir"};
        EXPECT_FALSE(dbi.SetConfig(config));
        EXPECT_FALSE(dbi.SetConfig(config));
    }
    GlobalMockObject::verify();
}

TEST(CustomDBI, convert_call_llvm_objcopy_failed_return_false)
{
    MOCKER(PipeCall)
        .stubs()
        .will(returnValue(false));
    CustomDBI dbi;
    dbi.initFunc_ = (CustomDBI::PluginInitFunc)0x1;
    BinaryInstrumentation::Config config;
    config.archName = "arch";
    dbi.SetConfig(config);
    EXPECT_FALSE(dbi.Convert("newKernelFile", "oldKernelFile", "tilingKey"));
    GlobalMockObject::verify();
}

TEST(CustomDBI, convert_generate_ordering_file_failed_return_false)
{
    MOCKER(PipeCall)
        .stubs()
        .will(returnValue(true));
    MOCKER(&CustomDBI::GenerateOrderingFile)
        .stubs()
        .will(returnValue(false));
    CustomDBI dbi;
    dbi.initFunc_ = (CustomDBI::PluginInitFunc)0x1;
    BinaryInstrumentation::Config config;
    config.archName = "arch";
    dbi.SetConfig(config);
    EXPECT_FALSE(dbi.Convert("newKernelFile", "oldKernelFile", "tilingKey"));
    GlobalMockObject::verify();
}

TEST(CustomDBI, convert_ld_failed_return_false)
{
    MOCKER(PipeCall)
        .stubs()
        .will(returnValue(true))
        .then(returnValue(false));
    MOCKER(&CustomDBI::GenerateOrderingFile)
        .stubs()
        .will(returnValue(false));
    CustomDBI dbi;
    dbi.initFunc_ = (CustomDBI::PluginInitFunc)0x1;
    BinaryInstrumentation::Config config;
    config.archName = "arch";
    dbi.SetConfig(config);
    EXPECT_FALSE(dbi.Convert("newKernelFile", "oldKernelFile", "tilingKey"));
    GlobalMockObject::verify();
}

TEST(CustomDBI, convert_bisheng_tune_failed_return_false)
{
    MOCKER(PipeCall)
        .stubs()
        .will(returnValue(true))
        .then(returnValue(true))
        .then(returnValue(false));
    MOCKER(&CustomDBI::GenerateOrderingFile)
        .stubs()
        .will(returnValue(false));
    CustomDBI dbi;
    dbi.initFunc_ = (CustomDBI::PluginInitFunc)0x1;
    BinaryInstrumentation::Config config;
    config.archName = "arch";
    dbi.SetConfig(config);
    EXPECT_FALSE(dbi.Convert("newKernelFile", "oldKernelFile", "tilingKey"));
    GlobalMockObject::verify();
}

TEST(DBIFactory, create_not_nullptr)
{
    EXPECT_NE(DBIFactory::Instance().Create(BIType::CUSTOMIZE), nullptr);
    EXPECT_NE(DBIFactory::Instance().Create(BIType::BB_COUNT), nullptr);
    EXPECT_NE(DBIFactory::Instance().Create(BIType::PGO), nullptr);
}

TEST(DBIFactory, create_nullptr)
{
    EXPECT_EQ(DBIFactory::Instance().Create(BIType::MAX), nullptr);
}

/**
/* | 用例集 | BinaryInstrumentationTest
/* |测试函数| CustomDBI::Convert()
/* | 用例名 | convert_with_tune_log_path_write_log_on_failure
/* |用例描述| 执行测试函数，PipeCall失败时写入tune日志
*/
TEST(CustomDBI, convert_with_tune_log_path_write_log_on_failure)
{
    MOCKER(&CustomDBI::GenerateOrderingFile).stubs().will(returnValue(true));
    MOCKER(&CustomDBI::GenerateTempProbe).stubs().will(returnValue(true));
    MOCKER(&CustomDBI::GenerateKernelWithProbe).stubs().will(returnValue(true));
    MOCKER(chmod).stubs().will(returnValue(0));
    MOCKER(PipeCall).stubs().will(returnValue(false));
    MOCKER(&WriteStringToFile).expects(exactly(1)).will(returnValue(true));
    CustomDBI dbi;
    dbi.initFunc_ = StubInitFunc;
    BinaryInstrumentation::Config config;
    config.archName = "arch";
    config.tuneLogPath = "./tune.log";
    dbi.SetConfig(config);
    EXPECT_FALSE(dbi.Convert("newKernelFile", "oldKernelFile", "tilingKey"));
    GlobalMockObject::verify();
}

/**
/* | 用例集 | BinaryInstrumentationTest
/* |测试函数| CustomDBI::Convert()
/* | 用例名 | convert_with_tune_log_path_write_log_on_success
/* |用例描述| 执行测试函数，转换成功时写入tune日志
*/
TEST(CustomDBI, convert_with_tune_log_path_write_log_on_success)
{
    MOCKER(PipeCall).stubs().will(returnValue(true));
    MOCKER(chmod).stubs().will(returnValue(0));
    MOCKER(&WriteStringToFile).expects(exactly(1)).will(returnValue(true));
    MOCKER(&CustomDBI::GenerateOrderingFile).stubs().will(returnValue(true));
    MOCKER(&CustomDBI::GenerateTempProbe).stubs().will(returnValue(true));
    MOCKER(&CustomDBI::GenerateKernelWithProbe).stubs().will(returnValue(true));
    CustomDBI dbi;
    dbi.initFunc_ = StubInitFunc;
    BinaryInstrumentation::Config config;
    config.archName = "arch";
    config.tuneLogPath = "./tune.log";
    dbi.SetConfig(config);
    EXPECT_TRUE(dbi.Convert("newKernelFile", "oldKernelFile", "tilingKey"));
    GlobalMockObject::verify();
}

/**
/* | 用例集 | BinaryInstrumentationTest
/* |测试函数| CustomDBI::Convert()
/* | 用例名 | convert_without_tune_log_path_no_write
/* |用例描述| 执行测试函数，无tune日志路径时不写入日志
*/
TEST(CustomDBI, convert_without_tune_log_path_no_write)
{
    MOCKER(&CustomDBI::GenerateOrderingFile).stubs().will(returnValue(true));
    MOCKER(&CustomDBI::GenerateTempProbe).stubs().will(returnValue(true));
    MOCKER(&CustomDBI::GenerateKernelWithProbe).stubs().will(returnValue(true));
    MOCKER(chmod).stubs().will(returnValue(0));
    MOCKER(PipeCall).stubs().will(returnValue(false));
    MOCKER(&WriteStringToFile).expects(exactly(0));
    CustomDBI dbi;
    dbi.initFunc_ = StubInitFunc;
    BinaryInstrumentation::Config config;
    config.archName = "arch";
    config.tuneLogPath = "";
    dbi.SetConfig(config);
    EXPECT_FALSE(dbi.Convert("newKernelFile", "oldKernelFile", "tilingKey"));
    GlobalMockObject::verify();
}
