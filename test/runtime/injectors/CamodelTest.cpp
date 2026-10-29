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


#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#define private public
#include "runtime/inject_helpers/ProfConfig.h"
#undef private
#include <vector>
#include <gtest/gtest.h>
#include "camodel/Camodel.h"
#include "camodel/CamodelHelper.h"
#include "dvc_log_api.h"
#include "mockcpp/mockcpp.hpp"
#include "utils/FileSystem.h"

using namespace std;

DvcLogCbFnUnion_t gCallbacks[4];
string gReceivedData;

int RecordDvcCallback(DvcLogType_t logType, DvcLogCbFnUnion_t fnUnion)
{
    gCallbacks[static_cast<int>(logType)] = fnUnion;
    return 0;
}

void RecordMsg(ProfConfig *b, const string &msg)
{
    gReceivedData = msg;
}

class CamodelTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        // only mock once in all camodel test case
        MOCKER(&GetEnv).stubs().will(returnValue(string("true")));
        MOCKER(&DvcSetLogLevelOrigin).stubs().will(returnValue(int(0)));
        MOCKER(&DvcAttachLogCallbackOrigin).stubs().will(invoke(RecordDvcCallback));
        MOCKER(&ProfConfig::SendMsg).stubs().will(invoke(RecordMsg));
        MessageOfProfConfig conf{};
        conf.pmSamplingEnable = true;
        ProfConfig::Instance().Init(conf);
        CamodelHelper::Instance().Enable();
        CamodelCtor();
    }

    static void TearDownTestCase()
    {
        ProfConfig::Instance().Reset();
        GlobalMockObject::verify();
    }

    void SetUp() override { }

    void TearDown() override { }
};

TEST_F(CamodelTest, mock_DvcAttachLogCallback_then_call_dvc_instr_poped_log_expect_receive_data)
{
    vector<char> desc(500, 'a');
    DvcInstrLogEntry_t instrLog{
        0, 0, 1, desc.data(), desc.data()
    };
    gReceivedData.clear();
    auto cb = gCallbacks[static_cast<uint32_t>(DvcLogType::DVC_INSTR_POPPED_LOG)].instrLogCb;
    cb(0, &instrLog);
    // 我们需要等待监听线程接受到消息后并处理，否则该函数就会结束，mock也结束，那么就会报错
    sleep(1);
    ASSERT_GT(gReceivedData.length(), 0);
}

TEST_F(CamodelTest, mock_DvcAttachLogCallback_then_call_dvc_instr_log_expect_receive_data)
{
    vector<char> desc(500, 'a');
    DvcInstrLogEntry_t instrLog{
        0, 0, 1, desc.data(), desc.data()
    };
    gReceivedData.clear();
    auto cb = gCallbacks[static_cast<uint32_t>(DvcLogType::DVC_INSTR_LOG)].instrLogCb;
    cb(0, &instrLog);
    // 我们需要等待监听线程接受到消息后并处理，否则该函数就会结束，mock可能也结束，那么就会报错
    sleep(1);
    ASSERT_GT(gReceivedData.length(), 0);
}

TEST_F(CamodelTest, mock_DvcAttachLogCallback_then_call_dvc_mte_log_expect_receive_data)
{
    DvcMteLogEntry_t mteSendLog{
        0, 0, "send_cmd", {"intf", 0, 1, 1, 1, 1, 1, 1}
    };
    gReceivedData.clear();
    auto cb = gCallbacks[static_cast<uint32_t>(DvcLogType::DVC_MTE_LOG)].mteLogCb;
    cb(0, &mteSendLog);
    // 我们需要等待监听线程接受到消息后并处理，否则该函数就会结束，mock可能也结束，那么就会报错
    sleep(1);
    ASSERT_GT(gReceivedData.length(), 0);
    gReceivedData.clear();
    DvcMteLogEntry_t mteRecvLog {
        0, 0, "recv_rsp", {"intf", 0, 1, 1, 1, 1, 1}
    };
    cb(0, &mteRecvLog);
    sleep(1);
    ASSERT_GT(gReceivedData.length(), 0);
}

TEST_F(CamodelTest, mock_DvcAttachLogCallback_then_call_dvc_icache_log_expect_receive_data)
{
    gReceivedData.clear();
    DvcIcacheLogEntry_t icacheLog {
        0, 0, "miss_read", {0, 1, 1, 1}
    };
    auto cb = gCallbacks[static_cast<uint32_t>(DvcLogType::DVC_ICACHE_LOG)].icacheLogCb;
    cb(0, &icacheLog);
    // 我们需要等待监听线程接受到消息后并处理，否则该函数就会结束，mock可能也结束，那么就会报错
    sleep(1);
    ASSERT_GT(gReceivedData.length(), 0);
}

namespace {
std::mutex g_messageMutex;
std::condition_variable g_messageReady;
std::vector<std::string> g_messages;
void RecordMessage(ProfConfig *, const std::string &message) {
    std::lock_guard<std::mutex> guard(g_messageMutex);
    g_messages.push_back(message);
    g_messageReady.notify_all();
}

std::string WaitForMessage(size_t index) {
    std::unique_lock<std::mutex> guard(g_messageMutex);
    if (!g_messageReady.wait_for(guard, std::chrono::seconds(2), [index] { return g_messages.size() > index; })) {
        return {};
    }
    return g_messages[index];
}
}
class CamodelV2Test : public testing::Test {
protected:
    void SetUp() override {
        MOCKER(&GetEnv).stubs().will(returnValue(std::string("true")));
        previousSocVersion_ = ProfConfig::Instance().socVersion_;
        ProfConfig::Instance().socVersion_ = "Ascend950DT_950x";
        MessageOfProfConfig config{};
        config.pmSamplingEnable = true;
        ProfConfig::Instance().Init(config);
        CamodelHelper::Instance().Enable();
        {
            std::lock_guard<std::mutex> guard(g_messageMutex);
            g_messages.clear();
        }
        MOCKER(&DvcSetLogLevelOrigin).stubs().will(returnValue(0));
        MOCKER(&DvcAttachLogCallbackOrigin).stubs().will(invoke(RecordDvcCallback));
        MOCKER(&ProfConfig::SendMsg).stubs().will(invoke(RecordMessage));
        CamodelCtor();
    }

    void TearDown() override {
        CamodelHelper::Instance().SendSync();
        GlobalMockObject::verify();
        ProfConfig::Instance().socVersion_ = previousSocVersion_;
    }

private:
    std::string previousSocVersion_;
};

TEST_F(CamodelV2Test, forwards_v2_fields_and_discards_invalid_optional_json) {
    struct Case {
        DvcLogType_t logType;
        uint64_t time;
        DvcInstrLogEntryV2_t entry;
        ProfPacketType packetType;
        const char *expectedJson;
    };
    const std::array<Case, 2> cases{{
        {DvcLogType::DVC_INSTR_LOG, 123, {3, 1, 0x9000, "MOV_XD_SPR", nullptr, R"({"burst_len":128})"},
            ProfPacketType::INSTR_LOG, R"({"burst_len":128})"},
        {DvcLogType::DVC_INSTR_POPPED_LOG, 456, {4, 2, 0x9010, "MOV_SRC", nullptr, "{invalid"},
            ProfPacketType::POPPED_LOG, ""}
    }};
    for (size_t i = 0; i < cases.size(); ++i) {
        auto callback = gCallbacks[static_cast<size_t>(cases[i].logType)].instrLogCbV2;
        ASSERT_NE(callback, nullptr);
        callback(cases[i].time, &cases[i].entry);
        const std::string message = WaitForMessage(i);
        ASSERT_EQ(message.size(), sizeof(ProfPacketHead) + sizeof(Common::DvcInstrLogV2));
        ProfPacketHead head{};
        Common::DvcInstrLogV2 payload{};
        std::memcpy(&head, message.data(), sizeof(head));
        std::memcpy(&payload, message.data() + sizeof(head), sizeof(payload));
        EXPECT_EQ(head.type, cases[i].packetType);
        EXPECT_EQ(payload.time, cases[i].time);
        EXPECT_EQ(payload.pc, cases[i].entry.pc);
        EXPECT_EQ(payload.coreId, cases[i].entry.core_id);
        EXPECT_EQ(payload.subCoreId, cases[i].entry.sub_core_id);
        EXPECT_STREQ(payload.decodeDescr, cases[i].entry.decode_descr);
        EXPECT_STREQ(payload.extendParamsJson, cases[i].expectedJson);
    }
    auto callback = gCallbacks[static_cast<size_t>(DvcLogType::DVC_INSTR_LOG)].instrLogCbV2;
    ASSERT_NE(callback, nullptr);
    DvcInstrLogEntryV2_t instruction{3, 1, 0x9000, "", nullptr, nullptr};
    callback(123, &instruction);
    CamodelHelper::Instance().SendSync();
    std::lock_guard<std::mutex> guard(g_messageMutex);
    EXPECT_EQ(g_messages.size(), cases.size());
}
