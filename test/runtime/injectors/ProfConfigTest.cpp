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
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "runtime/inject_helpers/ProfConfig.h"
#undef private
#include <string>
#include <vector>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#include "utils/FileSystem.h"
#include "utils/Environment.h"
#include "mockcpp/mockcpp.hpp"
#include "core/DomainSocket.h"
#include "core/LocalProcess.h"
#include "utils/Serialize.h"

constexpr uint64_t MEM_ADDR = 0x12c045400000U;
constexpr uint64_t MEM_SIZE = 0x1000U;

TEST(ProfConfigTest, prof_config_get_message)
{
    MessageOfProfConfig messageOfProfConfig;
    messageOfProfConfig.isSimulator = true;
    MOCKER(&setsockopt).stubs().will(returnValue(-1));
    MOCKER(&write).stubs().will(returnValue(4));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(static_cast<int>(sizeof(ProfPacketHead))));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    std::string msg = Serialize(messageOfProfConfig);
    MOCKER(&LocalProcess::Wait).stubs().with(outBound(msg), any())
                                       .will(returnValue(0)).then(returnValue(sizeof(messageOfProfConfig)));
    ProfConfig::Instance().Reset();
    ASSERT_TRUE(ProfConfig::Instance().GetConfig().isSimulator);
    GlobalMockObject::verify();
}

TEST(ProfConfigTest, prof_config_get_output_path_from_remote_error)
{
    MessageOfProfConfig messageOfProfConfig;
    messageOfProfConfig.isSimulator = true;
    MOCKER(&setsockopt).stubs().will(returnValue(-1));
    MOCKER(&write).stubs().will(returnValue(4));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(4));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    std::string lStr(1025, 's');
    ASSERT_EQ(ProfConfig::Instance().GetOutputPathFromRemote(lStr, 2), "");
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(-1)).then(returnValue(-1));
    ASSERT_EQ(ProfConfig::Instance().GetOutputPathFromRemote("aa", 2), "");
    ASSERT_EQ(ProfConfig::Instance().GetOutputPathFromRemote("aa", 2), "");
    MOCKER(&LocalProcess::Wait).stubs().will(returnValue(-1));
    ASSERT_EQ(ProfConfig::Instance().GetOutputPathFromRemote("aa", 2), "");
    GlobalMockObject::verify();
}

TEST(ProfConfigTest, prof_config_get_output_path_from_remote_success)
{
    MessageOfProfConfig messageOfProfConfig;
    messageOfProfConfig.isSimulator = true;
    MOCKER(&setsockopt).stubs().will(returnValue(-1));
    MOCKER(&write).stubs().will(returnValue(4));
    MOCKER(&LocalProcess::Notify).stubs()
        .will(returnValue(static_cast<int>(sizeof(ProfPacketHead) + sizeof(ProfDataPathConfig))));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    std::string lStr = "aaaa";
    std::string msg = "sssssss\n";
    MOCKER(&LocalProcess::Wait).stubs().with(outBound(msg), any())
                                       .will(returnValue(0)).then(returnValue(sizeof(messageOfProfConfig)));
    ASSERT_EQ(ProfConfig::Instance().GetOutputPathFromRemote(lStr, 2), "sssssss");
    GlobalMockObject::verify();
}

TEST(ProfConfigTest, prof_config_post_notify)
{
    MessageOfProfConfig messageOfProfConfig;
    messageOfProfConfig.isSimulator = true;
    ProcessCtrl::Req req;
    req.done = 1;
    ProcessCtrl::Rsp rsp;
    std::string msg(sizeof(rsp), '\0');
    msg.copy(reinterpret_cast<char *>(&rsp), sizeof(ProcessCtrl::Rsp));

    ProfConfig::Instance().profConfig_.killAdvance = true;
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(static_cast<int>(sizeof(req) + sizeof(ProfPacketHead))));
    MOCKER(&LocalProcess::Wait).stubs().with(outBound(msg), any())
                                       .will(returnValue(sizeof(ProcessCtrl::Rsp)));
    ASSERT_EQ(ProfConfig::Instance().PostNotify(rsp), true);
    GlobalMockObject::verify();
}

TEST(ProfConfigTest, get_plugin_path)
{
    std::string pathNull;
    std::string path = "/home/111/Ascend";
    MOCKER(&ProfConfig::GetMsopprofPath).expects(exactly(2)).will(returnValue(pathNull)).then(returnValue(path));
    EXPECT_EQ(ProfConfig::Instance().GetPluginPath(), "");
    EXPECT_EQ(ProfConfig::Instance().GetPluginPath(), "/home/111/Ascend/lib64/libprofplugin_memorychart.so");

    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(1));
    ProfConfig::Instance().RequestLogTranslate(path, "kn");
    ProfConfig::Instance().NotifyStopTransLog();
    GlobalMockObject::verify();
}

TEST(ProfConfigTest, request_log_trans_stop_notify_suc)
{
    GlobalMockObject::verify();
    std::string path = "/home/111/Ascend";
    ProfPacketHead head1{ProfPacketType::COLLECT_START, sizeof(CollectLogStart)};
    std::string msg1 = Serialize(head1);

    ProfPacketHead stopHead1{ProfPacketType::PROF_FINISH, 0};
    std::string msg2 = Serialize(stopHead1);
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(msg1.size())).then(returnValue(msg2.size()));
    std::string msg = "SUC";
    MOCKER(&LocalProcess::Wait).stubs().with(outBound(msg), any())
            .will(returnValue(3));

    testing::internal::CaptureStdout();
    ProfConfig::Instance().RequestLogTranslate(path, "kn");
    ProfConfig::Instance().NotifyStopTransLog();
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_NE(capture.find("ERROR"), std::string::npos);
    GlobalMockObject::verify();
}

TEST(ProfConfigTest, get_block_for_dbi_expect_return_correct_value)
{
    EXPECT_EQ(GetCoreNumForDbi(65), MAX_BLOCK);
    EXPECT_EQ(GetCoreNumForDbi(30), 90);
    GlobalMockObject::verify();
}

/**
 * | 用例集 | ProfConfigTest
 * |测试函数| ProfConfig::GetInstrTimelinePipe()
 * | 用例名 | test_get_instr_timeline_pipe_expect_lower
 * |用例描述| instrTimelinePipe格式转换正确
 */
TEST(ProfConfigTest, test_get_instr_timeline_pipe_expect_lower)
{
    // 测试为空
    ProfConfig::Instance().profConfig_.instrTimelinePipe[0] = '\0';
    EXPECT_EQ(ProfConfig::Instance().GetInstrTimelinePipe(), "cube,fixp,vector,mte1,mte2,mte3");
    // 单个pipe转换
    strcpy(ProfConfig::Instance().profConfig_.instrTimelinePipe, "CUBE");
    EXPECT_EQ(ProfConfig::Instance().GetInstrTimelinePipe(), "cube");
    // 多个pipe转换
    strcpy(ProfConfig::Instance().profConfig_.instrTimelinePipe, "CUBE|fixp|Vector");
    EXPECT_EQ(ProfConfig::Instance().GetInstrTimelinePipe(), "cube,fixp,vector");
}
