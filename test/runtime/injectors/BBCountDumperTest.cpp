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
#include "mockcpp/mockcpp.hpp"

#define private public
#include "runtime/inject_helpers/BBCountDumper.h"
#undef private

#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/DomainSocket.h"
#include "runtime/inject_helpers/DBITask.h"
#include "runtime/inject_helpers/KernelContext.h"

using namespace std;

TEST(BBCountDumper, test_replace_stub_func_expect_success)
{
    KernelMatcher::Config config{};
    BBCountDumper::Instance().Init("./", config);

    MOCKER_CPP(&RunDBITask, bool(*)(const StubFunc**))
        .stubs()
        .will(returnValue(true));
    MOCKER(IsPathExists).stubs().will(returnValue(true));
    MOCKER_CPP(&KernelContext::GetRegisterId, uint64_t(KernelContext::*)(uint64_t))
        .stubs()
        .will(returnValue(uint64_t(1)));
    EXPECT_TRUE(BBCountDumper::Instance().Replace(nullptr, 1));
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(BBCountDumper, test_replace_handle_expect_success)
{
    KernelMatcher::Config config{};
    BBCountDumper::Instance().Init("./", config);

    MOCKER_CPP(&RunDBITask, bool(*)(void**, const uint64_t))
        .stubs()
        .will(returnValue(true));
    MOCKER(IsPathExists).stubs().will(returnValue(true));
    MOCKER_CPP(&KernelContext::GetRegisterId, uint64_t(KernelContext::*)(uint64_t))
        .stubs()
        .will(returnValue(uint64_t(1)));
    EXPECT_TRUE(BBCountDumper::Instance().Replace(nullptr, 1, 1));
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(BBCountDumper, gen_extra_and_return_name)
{
    KernelMatcher::Config config{};
    BBCountDumper::Instance().Reset();
    std::string fileName = "extra.o";
    rtDevBinary_t *stubBin = nullptr;
    vector<uint8_t> mockData{0, 1, 2};
    rtDevBinary_t bin;
    bin.data = reinterpret_cast<void *>(mockData.data());
    bin.length = mockData.size() * sizeof(uint8_t);
    MOCKER(aclrtMemcpyImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));

    MOCKER(&BBCountDumper::GetOutFileName).stubs().will(returnValue(fileName));
    MOCKER(WriteBinary).stubs().will(returnValue(1));
    EXPECT_EQ(BBCountDumper::Instance().GenExtraAndReturnName("path", 1, 1, mockData.data()), fileName + ".0");
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(BBCountDumper, get_output_dir)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    MOCKER(&LocalProcess::Wait).stubs().will(returnValue(0)).then(returnValue(1));
    BBCountDumper::Instance().Reset();
    BBCountDumper::Instance().GetOutputDir();
    EXPECT_TRUE(BBCountDumper::Instance().outputDir_.empty());
    BBCountDumper::Instance().GetOutputDir();
    EXPECT_EQ(BBCountDumper::Instance().outputDir_, string{"GetOutputPath"});
    BBCountDumper::Instance().Reset();
    GlobalMockObject::verify();
}

TEST(BBCountDumper, dump_bbb_data_expect_return_true)
{
    MOCKER(WriteBinary).stubs().will(returnValue(static_cast<size_t>(1)));
    MOCKER(aclrtMemcpyImplOrigin).stubs().will(returnValue(0));
    MOCKER(aclrtFreeImplOrigin).stubs().will(returnValue(0));
    BBCountDumper::Instance().Reset();
    constexpr uint64_t memSize = 128;
    EXPECT_TRUE(BBCountDumper::Instance().DumpBBData(0, memSize, nullptr));
    GlobalMockObject::verify();
}

TEST(BBCountDumper, dump_bbb_data_expect_return_false)
{
    BBCountDumper::Instance().Reset();
    MOCKER(WriteBinary).stubs().will(returnValue(static_cast<size_t>(0)));
    MOCKER(aclrtMemcpyImplOrigin).stubs().will(returnValue(0));
    MOCKER(aclrtFreeImplOrigin).stubs().will(returnValue(0));
    BBCountDumper::Instance().Reset();
    constexpr uint64_t memSize = 128;
    EXPECT_FALSE(BBCountDumper::Instance().DumpBBData(0, memSize, nullptr));
    GlobalMockObject::verify();
}

TEST(BBCountDumper, GenExtraAndReturnName_return_empty_string)
{
    BBCountDumper::Instance().Reset();
    EXPECT_TRUE(BBCountDumper::Instance().GenExtraAndReturnName("", 0, 0, nullptr).empty());
    MOCKER(aclrtMemcpyImplOrigin).stubs().will(returnValue(1));
    EXPECT_TRUE(BBCountDumper::Instance().GenExtraAndReturnName("", 0, 128, nullptr).empty());
    GlobalMockObject::verify();
}

TEST(BBCountDumper, SaveBlockMapFile_expect_failed)
{
    string path = "/not_exist";
    BBCountDumper::Instance().Reset();
    MOCKER(JoinPath).stubs().will(returnValue(path));
    BBCountDumper::Instance().SaveBlockMapFile(0, 0, path);
    MOCKER(IsPathExists).stubs().will(returnValue(true));
    BBCountDumper::Instance().SaveBlockMapFile(0, 0, path);
    MOCKER(Chmod).stubs().will(returnValue(true));
    BBCountDumper::Instance().SaveBlockMapFile(0, 0, path);
    GlobalMockObject::verify();
}
