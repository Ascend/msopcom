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
#include <thread>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <unistd.h>
#include "nlohmann/json.hpp"
#include "mockcpp/mockcpp.hpp"
#define private public
#include "runtime/inject_helpers/MsTx.h"
#undef private
#include "ascendcl/AscendclOrigin.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "utils/FileSystem.h"

using namespace MstxAPI;

namespace {
const std::string &ReadCurrentTimelineTestEnv(const std::string &key) {
    thread_local std::string value;
    const char *current = std::getenv(key.c_str());
    value = current != nullptr ? current : "";
    return value;
}
}

class TimelineLaunchTest : public testing::Test {
protected:
    void SetUp() override {
        GlobalMockObject::verify();
        const char *previous = std::getenv(DEVICE_PROF_DUMP_PATH_ENV);
        hadDumpPath_ = previous != nullptr;
        savedDumpPath_ = previous != nullptr ? previous : "";
        char directory[] = "/tmp/msopscommon_timeline_XXXXXX";
        const char *created = mkdtemp(directory);
        ASSERT_NE(created, nullptr);
        directory_ = created;
        ASSERT_EQ(setenv(DEVICE_PROF_DUMP_PATH_ENV, directory_.c_str(), 1), 0);
        // 产品GetEnv按进程缓存，这里用当前环境值让每个用例读到各自的临时目录。
        MOCKER(&GetEnv).stubs().will(invoke(ReadCurrentTimelineTestEnv));
        MessageOfProfConfig config{};
        config.mstxProfConfig.isMstxEnable = true;
        ProfConfig::Instance().Init(config);
        ProfConfig::Instance().isRangeReplay_ = false;
        MsTx::Instance().MstxAttrReset(true);
    }

    void TearDown() override {
        GlobalMockObject::verify();
        MsTx::Instance().MstxAttrReset(true);
        ProfConfig::Instance().Init(MessageOfProfConfig{});
        ProfConfig::Instance().isRangeReplay_ = false;
        if (hadDumpPath_) {
            setenv(DEVICE_PROF_DUMP_PATH_ENV, savedDumpPath_.c_str(), 1);
        } else {
            unsetenv(DEVICE_PROF_DUMP_PATH_ENV);
        }
        if (!directory_.empty()) {
            RemoveAll(directory_);
        }
    }

    void Select(size_t index, const std::string &mstx, const std::string &kernel) {
        ASSERT_LT(index, MAX_TIMELINE_MERGE_CONFIG_COUNT);
        auto &selection = ProfConfig::Instance().profConfig_.timelineMergeConfigs[index];
        ASSERT_LT(mstx.size(), sizeof(selection.mstxName));
        ASSERT_LT(kernel.size(), sizeof(selection.kernelName));
        std::copy(mstx.begin(), mstx.end(), selection.mstxName);
        selection.mstxName[mstx.size()] = '\0';
        std::copy(kernel.begin(), kernel.end(), selection.kernelName);
        selection.kernelName[kernel.size()] = '\0';
    }

    std::vector<nlohmann::json> Records() const {
        std::ifstream stream(directory_ + "/timeline_mstx.json");
        std::vector<nlohmann::json> records;
        std::string line;
        while (std::getline(stream, line)) {
            records.push_back(nlohmann::json::parse(line));
        }
        return records;
    }

    bool hadDumpPath_ = false;
    std::string savedDumpPath_;
    std::string directory_;
};

/**
/* | 用例集 | TimelineLaunchTest
/* |测试函数| MsTx::RecordTimelineLaunch()
/* | 用例名 | prefix_and_wildcard_matches_expect_only_selected_json_records_in_order
/* |用例描述| 同一范围下前缀与通配符匹配，仅所选配置按索引追加JSON记录
*/
TEST_F(TimelineLaunchTest, prefix_and_wildcard_matches_expect_only_selected_json_records_in_order) {
    Select(0, "mstx1", "kernel");
    Select(1, "mstx1", "*_fused");
    Select(2, "inactive", "kernel");
    Select(3, "mstx1", "other");
    Select(5, "mstx1", "kernel"); // 第4项为空，后面的第5项不可被读取。
    auto &mstx = MsTx::Instance();
    const auto range = mstx.MstxRangeAdd("mstx1");
    const std::string output = "/capture/\"quoted\"/back\\slash/dump";
    mstx.RecordTimelineLaunch(3, "kernel_fused", output);
    mstx.RecordTimelineLaunch(7, "kernel_plain", "/second/dump");
    mstx.MstxRangeEnd(range);

    const auto records = Records();
    ASSERT_EQ(records.size(), 3U);
    EXPECT_EQ(records[0], (nlohmann::json{{"config_index", 0}, {"device_id", 3}, {"output_path", output}}));
    EXPECT_EQ(records[1], (nlohmann::json{{"config_index", 1}, {"device_id", 3}, {"output_path", output}}));
    EXPECT_EQ(records[2],
        (nlohmann::json{{"config_index", 0}, {"device_id", 7}, {"output_path", "/second/dump"}}));
}

/**
/* | 用例集 | TimelineLaunchTest
/* |测试函数| MsTx::RecordTimelineLaunch()
/* | 用例名 | invalid_arguments_or_no_active_range_expect_no_metadata_file
/* |用例描述| 空算子名、空输出目录或无活动范围时，不创建timeline_mstx.json记录文件
*/
TEST_F(TimelineLaunchTest, invalid_arguments_or_no_active_range_expect_no_metadata_file) {
    Select(0, "mstx1", "kernel");
    auto &mstx = MsTx::Instance();
    mstx.RecordTimelineLaunch(0, "kernel", "/output/dump");
    const auto range = mstx.MstxRangeAdd("mstx1");
    mstx.RecordTimelineLaunch(0, "", "/output/dump");
    mstx.RecordTimelineLaunch(0, "kernel", "");
    mstx.MstxRangeEnd(range);
    mstx.RecordTimelineLaunch(0, "kernel", "/output/dump");
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = false;
    mstx.RecordTimelineLaunch(0, "kernel", "/output/dump");
    EXPECT_FALSE(IsPathExists(directory_ + "/timeline_mstx.json"));
}

/**
/* | 用例集 | TimelineLaunchTest
/* |测试函数| MsTx::RecordTimelineLaunch()
/* | 用例名 | global_mstx_filter_expect_only_enabled_selection_recorded
/* |用例描述| 全局mstxEnabledMessage只允许mstx2时，仅记录对应配置索引1
*/
TEST_F(TimelineLaunchTest, global_mstx_filter_expect_only_enabled_selection_recorded) {
    Select(0, "mstx1", "kernel");
    Select(1, "mstx2", "kernel");
    auto &setting = ProfConfig::Instance().profConfig_.mstxProfConfig;
    std::copy_n("mstx2", sizeof("mstx2"), setting.mstxEnabledMessage);
    auto &mstx = MsTx::Instance();
    const auto first = mstx.MstxRangeAdd("mstx1");
    const auto second = mstx.MstxRangeAdd("mstx2");
    mstx.RecordTimelineLaunch(2, "kernel", "/enabled/dump");
    mstx.MstxRangeEnd(second);
    mstx.RecordTimelineLaunch(2, "kernel", "/disabled/dump");
    mstx.MstxRangeEnd(first);
    const auto records = Records();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records[0],
        (nlohmann::json{{"config_index", 1}, {"device_id", 2}, {"output_path", "/enabled/dump"}}));
}

/**
/* | 用例集 | TimelineLaunchTest
/* |测试函数| MsTx::RecordTimelineLaunch()
/* | 用例名 | four_thread_launches_expect_80_complete_and_thread_isolated_records
/* |用例描述| 4线程各记录20次launch，共80条记录且各线程内保持提交顺序
*/
TEST_F(TimelineLaunchTest, four_thread_launches_expect_80_complete_and_thread_isolated_records) {
    constexpr size_t threads = 4;
    constexpr size_t launches = 20;
    for (size_t index = 0; index < threads; ++index) {
        Select(index, "mstx" + std::to_string(index), "same_op");
    }
    std::vector<std::thread> workers;
    for (size_t index = 0; index < threads; ++index) {
        workers.emplace_back([index] {
            auto &mstx = MsTx::Instance();
            const auto range = mstx.MstxRangeAdd("mstx" + std::to_string(index));
            for (size_t launch = 0; launch < launches; ++launch) {
                mstx.RecordTimelineLaunch(static_cast<int32_t>(index), "same_op",
                    "/device" + std::to_string(index) + "/" + std::to_string(launch) + "/dump");
            }
            mstx.MstxRangeEnd(range);
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }
    MsTx::Instance().RecordTimelineLaunch(0, "same_op", "/main_thread/dump");
    const auto records = Records();
    ASSERT_EQ(records.size(), threads * launches);
    std::vector<size_t> nextLaunch(threads, 0);
    std::set<std::string> paths;
    for (const auto &record : records) {
        const size_t index = record["config_index"].get<size_t>();
        ASSERT_LT(index, threads);
        EXPECT_EQ(record["device_id"], index);
        const auto expected = "/device" + std::to_string(index) + "/" +
            std::to_string(nextLaunch[index]++) + "/dump";
        EXPECT_EQ(record["output_path"], expected);
        EXPECT_TRUE(paths.insert(record["output_path"].get<std::string>()).second);
    }
    for (auto count : nextLaunch) {
        EXPECT_EQ(count, launches);
    }
}

TEST(MsTx, prof_mstx_range_add_end_single_time)
{
    std::string msg = "1";
    MstxRangeId count = 0U;

    MsTx::Instance().MstxAttrReset(true);
    MstxRangeId id = MsTx::Instance().MstxRangeAdd(msg);
    ASSERT_EQ(id, count);
    ASSERT_EQ(MsTx::Instance().mstxRangeActiveId2MessageMap_.at(id), msg);
    ASSERT_EQ(MsTx::Instance().mstxRangeMessageUsageCountMap_.at(std::this_thread::get_id()).at(msg), 1);
    count++;
    MsTx::Instance().MstxRangeEnd(id);
    ASSERT_TRUE(MsTx::Instance().mstxRangeActiveId2MessageMap_.empty());
    ASSERT_TRUE(MsTx::Instance().mstxRangeMessageUsageCountMap_.empty());
    GlobalMockObject::verify();
}

TEST(MsTx, prof_mstx_range_add_end_single_time_range_replay)
{
    ProfConfig::Instance().isRangeReplay_ = true;
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = true;
    std::string msg = "1";
    MstxRangeId count = 0U;
    MsTx::Instance().MstxAttrReset(true);
    MOCKER(aclmdlRICaptureEndImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(aclmdlRIDestroyImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(&ProfDataCollect::RangeReplay).stubs().will(returnValue(true));

    MstxRangeId id = MsTx::Instance().MstxRangeAdd(msg);
    ASSERT_EQ(id, count);
    ASSERT_EQ(MsTx::Instance().mstxRangeActiveId2MessageMap_.at(id), msg);
    ASSERT_EQ(MsTx::Instance().mstxRangeMessageUsageCountMap_.at(std::this_thread::get_id()).at(msg), 1);
    count++;
    MsTx::Instance().MstxRangeEnd(id);
    ASSERT_TRUE(MsTx::Instance().mstxRangeActiveId2MessageMap_.empty());
    ASSERT_TRUE(MsTx::Instance().mstxRangeMessageUsageCountMap_.empty());

    ProfConfig::Instance().isRangeReplay_ = false;
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = false;
    GlobalMockObject::verify();
}

TEST(MsTx, prof_mstx_range_add_end_twice_overlapped)
{
    std::string msg = "1";
    MstxRangeId count = 0U;

    MsTx::Instance().MstxAttrReset(true);
    MstxRangeId id1 = MsTx::Instance().MstxRangeAdd("1");
    ASSERT_EQ(id1, count);
    count++;

    MstxRangeId id2 = MsTx::Instance().MstxRangeAdd("2");
    ASSERT_EQ(id2, count);
    count++;

    MsTx::Instance().MstxRangeEnd(id1);
    MsTx::Instance().MstxRangeEnd(id2);

    GlobalMockObject::verify();
}

TEST(MsTx, prof_mstx_range_add_end_twice_overlapped_range_replay)
{
    ProfConfig::Instance().isRangeReplay_ = true;
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = true;
    std::string msg1 = "1";
    std::string msg2 = "2";
    MstxRangeId count = 0U;

    MsTx::Instance().MstxAttrReset(true);
    auto threadId = std::this_thread::get_id();
    MstxRangeId id1 = MsTx::Instance().MstxRangeAdd(msg1);
    ASSERT_EQ(id1, count);
    count++;
    ASSERT_EQ(MsTx::Instance().mstxRangeActiveId2MessageMap_.at(id1), msg1);
    ASSERT_EQ(MsTx::Instance().mstxRangeMessageUsageCountMap_.at(threadId).at(msg1), 1);

    MstxRangeId id2 = MsTx::Instance().MstxRangeAdd(msg2);
    ASSERT_EQ(id2, count);
    count++;
    ASSERT_EQ(MsTx::Instance().mstxRangeActiveId2MessageMap_.at(id2), msg2);
    ASSERT_TRUE(MsTx::Instance().mstxRangeMessageUsageCountMap_.at(threadId).find(msg2) == MsTx::Instance().mstxRangeMessageUsageCountMap_.at(threadId).end());

    MsTx::Instance().MstxRangeEnd(id1);
    ASSERT_TRUE(MsTx::Instance().mstxRangeActiveId2MessageMap_.find(id1) == MsTx::Instance().mstxRangeActiveId2MessageMap_.end());
    ASSERT_TRUE(MsTx::Instance().mstxRangeMessageUsageCountMap_.empty());
    MsTx::Instance().MstxRangeEnd(id2);
    ASSERT_TRUE(MsTx::Instance().mstxRangeActiveId2MessageMap_.empty());
    ASSERT_TRUE(MsTx::Instance().mstxRangeMessageUsageCountMap_.empty());
    ProfConfig::Instance().isRangeReplay_ = false;
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = false;
    GlobalMockObject::verify();
}

TEST(MsTx, prof_mstx_range_add_empty)
{
    MstxRangeId count = 0U;

    MsTx::Instance().MstxAttrReset(true);
    MstxRangeId id = MsTx::Instance().MstxRangeAdd("");
    ASSERT_EQ(id, count);
    MsTx::Instance().MstxRangeEnd(id);
    GlobalMockObject::verify();
}

TEST(MsTx, prof_IsInMstxRange_expert_true_when_mstx_not_enable)
{
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = false;
    ProfConfig::Instance().profConfig_.mstxProfConfig.mstxEnabledMessage[NAME_MAX_LENGTH] = {'\0'};
    MstxProfConfig config;
    config.isMstxEnable = false;
    MsTx::Instance().MstxAttrReset(true);
    ASSERT_TRUE(MsTx::Instance().IsInMstxRange());
    GlobalMockObject::verify();
}

TEST(MsTx, prof_IsInMstxRange_expert_true_when_mstx_message_empty_but_usage_map_not_empty)
{
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = true;
    ProfConfig::Instance().profConfig_.mstxProfConfig.mstxEnabledMessage[NAME_MAX_LENGTH] = {'\0'};
    MsTx::Instance().MstxAttrReset(true);
    MstxRangeId id = MsTx::Instance().MstxRangeAdd("1");
    ASSERT_TRUE(MsTx::Instance().IsInMstxRange());
    MsTx::Instance().MstxRangeEnd(id);
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = false;
    GlobalMockObject::verify();
}

TEST(MsTx, prof_IsInMstxRange_expert_false_when_mstx_message_empty_and_usage_map_empty)
{
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = true;
    ProfConfig::Instance().profConfig_.mstxProfConfig.mstxEnabledMessage[NAME_MAX_LENGTH] = {'\0'};
    MsTx::Instance().MstxAttrReset(true);
    ASSERT_FALSE(MsTx::Instance().IsInMstxRange());
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = false;
    GlobalMockObject::verify();
}

TEST(MsTx, prof_IsInMstxRange_expert_true_message_in_usage_map)
{
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = true;
    ProfConfig::Instance().profConfig_.mstxProfConfig.mstxEnabledMessage[NAME_MAX_LENGTH] = {'\0'};
    ProfConfig::Instance().profConfig_.mstxProfConfig.mstxEnabledMessage[0] = {'1'};
    MsTx::Instance().MstxAttrReset(true);
    MstxRangeId id = MsTx::Instance().MstxRangeAdd("1");
    ASSERT_TRUE(MsTx::Instance().IsInMstxRange());
    MsTx::Instance().MstxRangeEnd(id);
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = false;
    ProfConfig::Instance().profConfig_.mstxProfConfig.mstxEnabledMessage[NAME_MAX_LENGTH] = {'\0'};
    GlobalMockObject::verify();
}

TEST(MsTx, prof_IsInMstxRange_expert_false_message_in_usage_map)
{
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = true;
    ProfConfig::Instance().profConfig_.mstxProfConfig.mstxEnabledMessage[NAME_MAX_LENGTH] = {'\0'};
    ProfConfig::Instance().profConfig_.mstxProfConfig.mstxEnabledMessage[0] = {'a'};
    MsTx::Instance().MstxAttrReset(true);
    MstxRangeId id = MsTx::Instance().MstxRangeAdd("1");
    ASSERT_FALSE(MsTx::Instance().IsInMstxRange());
    MsTx::Instance().MstxRangeEnd(id);
    ProfConfig::Instance().profConfig_.mstxProfConfig.isMstxEnable = false;
    ProfConfig::Instance().profConfig_.mstxProfConfig.mstxEnabledMessage[NAME_MAX_LENGTH] = {'\0'};
    GlobalMockObject::verify();
}

TEST(MsTx, sanitizer_mstx_domain_create_expect_equal)
{
    MsTx::Instance().MstxAttrReset(true);
    ASSERT_EQ(MsTx::Instance().mstxName2DomainMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomain2NameMap_.size(), 0);
    MsTx::Instance().MstxDomainCreateA("sample_1");
    ASSERT_EQ(MsTx::Instance().mstxName2DomainMap_.size(), 1);
    ASSERT_EQ(MsTx::Instance().mstxDomain2NameMap_.size(), 1);
    MsTx::Instance().MstxDomainCreateA("sample_1");
    ASSERT_EQ(MsTx::Instance().mstxName2DomainMap_.size(), 1);
    ASSERT_EQ(MsTx::Instance().mstxDomain2NameMap_.size(), 1);
    MsTx::Instance().MstxDomainCreateA("sample_2");
    ASSERT_EQ(MsTx::Instance().mstxName2DomainMap_.size(), 2);
    ASSERT_EQ(MsTx::Instance().mstxDomain2NameMap_.size(), 2);
}

TEST(MsTx, sanitizer_mstx_mem_heap_register_with_global_domain_expect_success)
{
    MsTx::Instance().MstxAttrReset(true);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().MstxMemHeapRegister({}, {}), nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_.size(), 0);
    MstxMemHeapDesc desc1{};
    int a = 1;
    desc1.typeSpecificDesc = &a;
    ASSERT_NE(MsTx::Instance().MstxMemHeapRegister({}, &desc1), nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 1);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[{}].size(), 1);
    ASSERT_NE(MsTx::Instance().MstxMemHeapRegister({}, &desc1), nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 2);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[{}].size(), 2);
    auto desc2 = std::make_shared<MstxMemHeapDesc>();
    desc2->typeSpecificDesc = &a;
    ASSERT_NE(MsTx::Instance().MstxMemHeapRegister({}, desc2.get()), nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 3);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[{}].size(), 3);
    MstxMemHeapDesc desc3{};
    desc3.typeSpecificDesc = &a;
    MstxMemHeapDesc *desc3Ptr = &desc3;
    ASSERT_NE(MsTx::Instance().MstxMemHeapRegister({}, desc3Ptr), nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 4);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[{}].size(), 4);
}

TEST(MsTx, sanitizer_mstx_mem_heap_register_with_un_global_domain_expect_success)
{
    MsTx::Instance().MstxAttrReset(true);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[{}].size(), 0);
    auto domain1 = MsTx::Instance().MstxDomainCreateA("sample_1");
    MstxMemHeapDesc desc{};
    int a = 1;
    desc.typeSpecificDesc = &a;
    ASSERT_NE(MsTx::Instance().MstxMemHeapRegister(domain1, &desc), nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 1);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain1].size(), 1);
    ASSERT_EQ(MsTx::Instance().MstxMemHeapRegister(domain1, nullptr), nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 1);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain1].size(), 1);
    ASSERT_NE(MsTx::Instance().MstxMemHeapRegister(domain1, &desc), nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 2);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain1].size(), 2);
    auto domain2 = MsTx::Instance().MstxDomainCreateA("sample_1");
    ASSERT_NE(MsTx::Instance().MstxMemHeapRegister(domain2, &desc), nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 3);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain2].size(), 3);
    ASSERT_EQ(MsTx::Instance().MstxMemHeapRegister(domain2, nullptr), nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 3);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain2].size(), 3);
    auto domain3 = MsTx::Instance().MstxDomainCreateA("sample_2");
    ASSERT_NE(MsTx::Instance().MstxMemHeapRegister(domain3, &desc), nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 4);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain3].size(), 1);
    ASSERT_EQ(MsTx::Instance().MstxMemHeapRegister(domain3, nullptr), nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 4);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain3].size(), 1);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain1].size(), 3);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain2].size(), 3);
}

TEST(MsTx, sanitizer_mstx_mem_heap_register_with_illegal_domain_expect_size_0)
{
    MsTx::Instance().MstxAttrReset(true);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_.size(), 0);
    MsTx::Instance().MstxDomainCreateA("sample_1");
    MsTx::Instance().MstxDomainCreateA("sample_2");
    MstxDomainRegistration illegalDomain{};
    MstxMemHeapDesc desc{};
    ASSERT_EQ(MsTx::Instance().MstxMemHeapRegister(&illegalDomain, &desc), nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_.size(), 0);
}

TEST(MsTx, sanitizer_mstx_mem_heap_unregister_with_global_domain_expect_success)
{
    MsTx::Instance().MstxAttrReset(true);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_.size(), 0);
    MstxMemHeapRangeDesc heapRangeDesc{};
    ASSERT_FALSE(MsTx::Instance().MstxMemHeapUnregister({}, {}, &heapRangeDesc));
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_.size(), 0);
    MstxMemHeapDesc desc{};
    int a = 1;
    desc.typeSpecificDesc = &a;
    auto memHeap1 = MsTx::Instance().MstxMemHeapRegister({}, &desc);
    ASSERT_NE(memHeap1, nullptr);
    auto memHeap2 = MsTx::Instance().MstxMemHeapRegister({}, &desc);
    ASSERT_NE(memHeap2, nullptr);
    ASSERT_NE(memHeap1, memHeap2);
    auto memHeap3 = MsTx::Instance().MstxMemHeapRegister({}, &desc);
    ASSERT_NE(memHeap3, nullptr);
    ASSERT_NE(memHeap2, memHeap3);
    auto memHeap4 = MsTx::Instance().MstxMemHeapRegister({}, &desc);
    ASSERT_NE(memHeap4, nullptr);
    ASSERT_NE(memHeap3, memHeap4);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 4);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[{}].size(), 4);
    ASSERT_TRUE(MsTx::Instance().MstxMemHeapUnregister({}, memHeap3, &heapRangeDesc));
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 3);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[{}].size(), 3);
    ASSERT_FALSE(MsTx::Instance().MstxMemHeapUnregister({}, memHeap3, &heapRangeDesc));
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 3);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[{}].size(), 3);
    ASSERT_TRUE(MsTx::Instance().MstxMemHeapUnregister({}, memHeap2, &heapRangeDesc));
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 2);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[{}].size(), 2);
    ASSERT_TRUE(MsTx::Instance().MstxMemHeapUnregister({}, memHeap4, &heapRangeDesc));
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 1);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[{}].size(), 1);
    ASSERT_TRUE(MsTx::Instance().MstxMemHeapUnregister({}, memHeap1, &heapRangeDesc));
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[{}].size(), 0);
    ASSERT_FALSE(MsTx::Instance().MstxMemHeapUnregister({}, memHeap1, &heapRangeDesc));
}

TEST(MsTx, sanitizer_mstx_mem_heap_unregister_with_unglobal_domain_expect_success)
{
    MsTx::Instance().MstxAttrReset(true);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_.size(), 0);
    auto domain1 = MsTx::Instance().MstxDomainCreateA("sample_1");
    auto domain2 = MsTx::Instance().MstxDomainCreateA("sample_2");
    MstxMemHeapDesc desc{};
    int a = 1;
    desc.typeSpecificDesc = &a;
    auto memHeap1 = MsTx::Instance().MstxMemHeapRegister(domain1, &desc);
    auto memHeap2 = MsTx::Instance().MstxMemHeapRegister(domain1, &desc);
    auto memHeap3 = MsTx::Instance().MstxMemHeapRegister(domain1, &desc);
    auto memHeap4 = MsTx::Instance().MstxMemHeapRegister(domain2, &desc);
    auto memHeap5 = MsTx::Instance().MstxMemHeapRegister(domain2, &desc);
    auto memHeap6 = MsTx::Instance().MstxMemHeapRegister(domain2, &desc);

    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 6);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain1].size(), 3);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain2].size(), 3);
    MstxMemHeapRangeDesc heapRangeDesc{};
    ASSERT_TRUE(MsTx::Instance().MstxMemHeapUnregister(domain1, memHeap3, &heapRangeDesc));
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 5);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain1].size(), 2);
    ASSERT_TRUE(MsTx::Instance().MstxMemHeapUnregister(domain1, memHeap2, &heapRangeDesc));
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 4);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain1].size(), 1);
    ASSERT_TRUE(MsTx::Instance().MstxMemHeapUnregister(domain1, memHeap1, &heapRangeDesc));
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 3);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain1].size(), 0);
    ASSERT_FALSE(MsTx::Instance().MstxMemHeapUnregister(domain1, memHeap1, &heapRangeDesc));
    ASSERT_TRUE(MsTx::Instance().MstxMemHeapUnregister(domain2, memHeap4, &heapRangeDesc));
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 2);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain2].size(), 2);
    ASSERT_TRUE(MsTx::Instance().MstxMemHeapUnregister(domain2, memHeap6, &heapRangeDesc));
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 1);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain2].size(), 1);
    ASSERT_TRUE(MsTx::Instance().MstxMemHeapUnregister(domain2, memHeap5, &heapRangeDesc));
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[domain2].size(), 0);
    ASSERT_FALSE(MsTx::Instance().MstxMemHeapUnregister(domain2, memHeap1, &heapRangeDesc));
}

TEST(MsTx, sanitizer_mstx_mem_heap_unregister_with_illegal_domain_expect_size_0)
{
    MsTx::Instance().MstxAttrReset(true);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_.size(), 0);
    MsTx::Instance().MstxDomainCreateA("sample_1");
    MsTx::Instance().MstxDomainCreateA("sample_2");
    MstxDomainRegistration illegalDomain{};
    MstxMemHeap heap{};
    MstxMemHeapRangeDesc heapRangeDesc{};
    ASSERT_FALSE(MsTx::Instance().MstxMemHeapUnregister(&illegalDomain, &heap, &heapRangeDesc));
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_.size(), 0);
}

TEST(MsTx, sanitizer_mstx_mem_heap_regions_register_with_global_domain_expect_success)
{
    MsTx::Instance().MstxAttrReset(true);
    ASSERT_EQ(MsTx::Instance().mstxRegionRangeDescMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomainRegionsMap_.size(), 0);
    MstxMemHeapDesc heapDesc{};
    int a = 1;
    heapDesc.typeSpecificDesc = &a;
    auto memPool = MsTx::Instance().MstxMemHeapRegister({}, &heapDesc);
    ASSERT_FALSE(MsTx::Instance().MstxMemRegionsRegister({}, {}).success);
    MstxMemRegionsRegisterBatch desc{};
    size_t regionCount1 = 2;
    desc.heap = memPool;
    MstxMemRegion *regions[regionCount1] = {};
    desc.regionDescArray = &a;
    desc.regionHandleArrayOut = regions;
    desc.heap = memPool;
    ASSERT_TRUE(MsTx::Instance().MstxMemRegionsRegister({}, &desc).success);
    ASSERT_EQ(MsTx::Instance().mstxRegionRangeDescMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomainRegionsMap_[{}].size(), 0);
    desc.regionCount = regionCount1;
    MstxMemVirtualRangeDesc rangeDesc[regionCount1] = {};
    for (size_t i = 0; i < regionCount1; ++i) {
        rangeDesc[i].deviceId = i;
        rangeDesc[i].size = i;
    }
    desc.regionDescArray = rangeDesc;
    MstxMemRegion *regionHandles[regionCount1] = {};
    desc.regionHandleArrayOut = regionHandles;
    ASSERT_TRUE(MsTx::Instance().MstxMemRegionsRegister({}, &desc).success);
    ASSERT_EQ(MsTx::Instance().mstxRegionRangeDescMap_.size(), regionCount1);
    ASSERT_EQ(MsTx::Instance().mstxDomainRegionsMap_[{}].size(), regionCount1);
}

TEST(MsTx, sanitizer_mstx_mem_heap_unregions_register_with_global_domain_expect_success)
{
    MsTx::Instance().MstxAttrReset(true);
    MstxMemRegionsRegisterBatch registerDesc{};
    size_t regionCount1 = 5;
    registerDesc.regionCount = regionCount1;
    MstxMemVirtualRangeDesc rangeDesc[regionCount1] = {};
    for (size_t i = 0; i < regionCount1; ++i) {
        rangeDesc[i].deviceId = i;
        rangeDesc[i].size = i;
    }
    MstxMemHeapDesc heapDesc{};
    int a = 1;
    heapDesc.typeSpecificDesc = &a;
    auto memPool = MsTx::Instance().MstxMemHeapRegister({}, &heapDesc);
    registerDesc.regionDescArray = rangeDesc;
    MstxMemRegion *regionHandles[regionCount1] = {};
    registerDesc.regionHandleArrayOut = regionHandles;
    registerDesc.regionDescArray = &a;
    registerDesc.heap = memPool;
    ASSERT_TRUE(MsTx::Instance().MstxMemRegionsRegister({}, &registerDesc).success);
    ASSERT_EQ(MsTx::Instance().mstxRegionRangeDescMap_.size(), regionCount1);
    ASSERT_EQ(MsTx::Instance().mstxDomainRegionsMap_[{}].size(), regionCount1);
    MstxMemRegionsUnregisterBatch unRegisterDesc{};
    size_t refCount = 2;
    MstxMemRegionRef regionRef[refCount] = {};
    for (size_t i = 0; i < refCount; ++i) {
        regionRef[i].refType = MstxMemRegionRefType::MSTX_MEM_REGION_REF_TYPE_HANDLE;
        regionRef[i].handle = registerDesc.regionHandleArrayOut[i];
    }
    unRegisterDesc.refCount = refCount;
    unRegisterDesc.refArray = regionRef;
    std::vector<MstxMemVirtualRangeDesc> rangeDescVec;
    ASSERT_TRUE(MsTx::Instance().MstxMemRegionsUnregister({}, &unRegisterDesc, rangeDescVec));
    ASSERT_EQ(MsTx::Instance().mstxRegionRangeDescMap_.size(), regionCount1 - refCount);
    ASSERT_EQ(MsTx::Instance().mstxDomainRegionsMap_[{}].size(), regionCount1 - refCount);
    ASSERT_EQ(rangeDescVec.size(), 2);
}

TEST(MsTx, sanitizer_mstx_mem_heap_unregister_with_global_domain_and_free_expect_success)
{
    MsTx::Instance().MstxAttrReset(true);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_.size(), 0);
    MstxMemHeapRangeDesc heapRangeDesc{};
    MsTx::Instance().MstxMemHeapUnregister({}, {}, &heapRangeDesc);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapDescMap_.size(), 0);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_.size(), 0);
    MstxMemVirtualRangeDesc rangeDesc{};
    rangeDesc.deviceId = 3;
    rangeDesc.size = 100;
    int * aPtr = new int;
    uint64_t aPtrUint = reinterpret_cast<uint64_t>(aPtr);
    rangeDesc.ptr = aPtr;
    MstxMemHeapDesc desc{};
    desc.typeSpecificDesc = &rangeDesc;
    auto memHeap1 = MsTx::Instance().MstxMemHeapRegister({}, &desc);
    delete aPtr;
    aPtr = nullptr;
    ASSERT_NE(memHeap1, nullptr);
    ASSERT_EQ(MsTx::Instance().mstxDomainMemHeapMap_[{}].size(), 1);
    auto &memHeapVec = MsTx::Instance().mstxDomainMemHeapMap_[{}];
    auto heapBasicDesc = MsTx::Instance().mstxDomainMemHeapDescMap_[{{}, memHeapVec.back().get()}];
    ASSERT_EQ(heapBasicDesc.rangeDesc.deviceId, 3);
    ASSERT_EQ(heapBasicDesc.rangeDesc.size, 100);
    ASSERT_EQ(reinterpret_cast<uint64_t>(heapBasicDesc.rangeDesc.ptr), aPtrUint);
}

TEST(MsTx, sanitizer_mstx_mem_heap_unregions_register_with_global_domain_and_free_expect_success)
{
    MsTx::Instance().MstxAttrReset(true);
    MstxMemRegionsRegisterBatch registerDesc{};
    size_t regionCount1 = 5;
    registerDesc.regionCount = regionCount1;
    MstxMemVirtualRangeDesc rangeDesc[regionCount1] = {};
    std::vector<int*> ptrVec;
    std::vector<uint64_t> ptrUintVec;
    for (size_t i = 0; i < regionCount1; ++i) {
        rangeDesc[i].deviceId = i;
        rangeDesc[i].size = i;
        int* ptr = new int;
        rangeDesc[i].ptr = ptr;
        ptrVec.push_back(ptr);
        ptrUintVec.push_back(reinterpret_cast<uint64_t>(ptr));
    }
    MstxMemHeapDesc heapDesc{};
    int a = 1;
    heapDesc.typeSpecificDesc = &a;
    auto memPool = MsTx::Instance().MstxMemHeapRegister({}, &heapDesc);
    registerDesc.regionDescArray = rangeDesc;
    MstxMemRegion *regionHandles[regionCount1] = {};
    registerDesc.regionHandleArrayOut = regionHandles;
    registerDesc.heap = memPool;
    ASSERT_TRUE(MsTx::Instance().MstxMemRegionsRegister({}, &registerDesc).success);
    ASSERT_EQ(MsTx::Instance().mstxRegionRangeDescMap_.size(), regionCount1);
    ASSERT_EQ(MsTx::Instance().mstxDomainRegionsMap_[{}].size(), regionCount1);
    for (int* &ptr : ptrVec) {
        delete ptr;
        ptr = nullptr;
    }
    MstxMemRegionsUnregisterBatch unRegisterDesc{};
    size_t refCount = 3;
    MstxMemRegionRef regionRef[refCount] = {};
    for (size_t i = 0; i < refCount; ++i) {
        regionRef[i].refType = MstxMemRegionRefType::MSTX_MEM_REGION_REF_TYPE_HANDLE;
        regionRef[i].handle = registerDesc.regionHandleArrayOut[i];
    }
    unRegisterDesc.refCount = refCount;
    unRegisterDesc.refArray = regionRef;
    std::vector<MstxMemVirtualRangeDesc> rangeDescVec;
    ASSERT_TRUE(MsTx::Instance().MstxMemRegionsUnregister({}, &unRegisterDesc, rangeDescVec));
    ASSERT_EQ(MsTx::Instance().mstxRegionRangeDescMap_.size(), regionCount1 - refCount);
    ASSERT_EQ(MsTx::Instance().mstxDomainRegionsMap_[{}].size(), regionCount1 - refCount);
    ASSERT_EQ(rangeDescVec.size(), refCount);
    size_t idx{};
    for (auto const &desc: rangeDescVec) {
        ASSERT_EQ(desc.deviceId, idx);
        ASSERT_EQ(desc.size, idx);
        ASSERT_EQ(reinterpret_cast<uint64_t>(desc.ptr), ptrUintVec[idx]);
        idx++;
    }
}

TEST(MsTx, sanitizer_mstx_mem_permissions_assign_with_unregistered_domain_name_expect_return_false)
{
    MsTx::Instance().MstxAttrReset(true);

    MstxAPI::MstxDomainRegistration unregisteredDomain{};
    std::vector<MstxAPI::MstxMemVirtualRangeDesc> rangeDescVec;
    ASSERT_FALSE(MsTx::Instance().MstxMemPermissionsAssign(&unregisteredDomain, nullptr, rangeDescVec));
}

TEST(MsTx, sanitizer_mstx_mem_permissions_assign_with_null_desc_expect_return_false)
{
    MsTx::Instance().MstxAttrReset(true);

    std::vector<MstxAPI::MstxMemVirtualRangeDesc> rangeDescVec;
    ASSERT_FALSE(MsTx::Instance().MstxMemPermissionsAssign(nullptr, nullptr, rangeDescVec));
    MstxAPI::MstxMemPermissionsAssignBatch assignBatch{};
    ASSERT_FALSE(MsTx::Instance().MstxMemPermissionsAssign(nullptr, &assignBatch, rangeDescVec));
}

TEST(MsTx, sanitizer_mstx_mem_permissions_assign_with_valid_desc_expect_get_correct_ranges)
{
    MsTx::Instance().MstxAttrReset(true);

    // register memory regions
    MstxAPI::MstxMemVirtualRangeDesc regions[2];
    regions[0].deviceId = 0;
    regions[0].ptr = reinterpret_cast<void const *>(0x100);
    regions[0].size = 100;
    regions[1].deviceId = 1;
    regions[1].ptr = reinterpret_cast<void const *>(0x200);
    regions[1].size = 200;
    MstxAPI::MstxRegionHandle handles[2];
    MstxAPI::MstxMemRegionsRegisterBatch regionBatch{};
    regionBatch.regionType = MstxMemType::MSTX_MEM_TYPE_VIRTUAL_ADDRESS;
    regionBatch.regionCount = 2;
    regionBatch.regionDescArray = regions;
    regionBatch.regionHandleArrayOut = handles;
    MsTx::Instance().MstxMemRegionsRegister(nullptr, &regionBatch);

    MstxAPI::MstxMemPermissionsAssignRegionsDesc assigns[2];
    assigns[0].flags = 0x01;
    assigns[0].region.refType = MstxAPI::MstxMemRegionRefType::MSTX_MEM_REGION_REF_TYPE_HANDLE;
    assigns[0].region.handle = handles[0];
    assigns[1].flags = 0x02;
    assigns[1].region.refType = MstxAPI::MstxMemRegionRefType::MSTX_MEM_REGION_REF_TYPE_HANDLE;
    assigns[1].region.handle = handles[1];
    MstxAPI::MstxMemPermissionsAssignBatch assignBatch{};
    assignBatch.regionCount = 2;
    assignBatch.regionDescArray = assigns;
    std::vector<MstxAPI::MstxMemVirtualRangeDesc> rangeDescVec;
    ASSERT_TRUE(MsTx::Instance().MstxMemPermissionsAssign(nullptr, &assignBatch, rangeDescVec));

    ASSERT_EQ(rangeDescVec.size(), 2);
    ASSERT_EQ(rangeDescVec[0].deviceId, 0);
    ASSERT_EQ(rangeDescVec[0].ptr, reinterpret_cast<void const *>(0x100));
    ASSERT_EQ(rangeDescVec[0].size, 100);
    ASSERT_EQ(rangeDescVec[1].deviceId, 1);
    ASSERT_EQ(rangeDescVec[1].ptr, reinterpret_cast<void const *>(0x200));
    ASSERT_EQ(rangeDescVec[1].size, 200);
}
