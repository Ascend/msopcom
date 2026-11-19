/*
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
 * Licensed under Mulan PSL v2.
 */
#include <gtest/gtest.h>
#include <cstring>
#define private public
#define protected public
#include "runtime/HijackedFunc.h"
#include "runtime/inject_helpers/ProfConfig.h"
#undef protected
#undef private
#include "mockcpp/mockcpp.hpp"
#include "runtime/RuntimeOrigin.h"
#include "runtime/inject_helpers/LaunchArgs.h"

namespace {
rtArgsEx_t *expectedArgs = nullptr;
uint8_t *expectedRecord = nullptr;
const void *expectedPrefix = nullptr;
uint32_t expectedPrefixSize = 0;

rtError_t CheckExpandedProbeArgs(rtArgsEx_t *args)
{
    EXPECT_EQ(args, expectedArgs);
    if (args != nullptr) {
        EXPECT_EQ(args->argsSize, 5U * sizeof(void *));
        if (args->argsSize == 5U * sizeof(void *)) {
            uint8_t *record = nullptr;
            std::memcpy(&record, static_cast<uint8_t *>(args->args) + 4U * sizeof(void *), sizeof(record));
            EXPECT_EQ(record, expectedRecord);
            // 扩展缓冲区必须逐字节保留原始参数作为前缀
            EXPECT_NE(args->args, nullptr);
            if (args->args != nullptr && expectedPrefix != nullptr) {
                EXPECT_EQ(std::memcmp(args->args, expectedPrefix, expectedPrefixSize), 0);
            }
        }
    }
    return RT_ERROR_NONE;
}

class InstrProfLaunchArgs : public ::testing::Test {
protected:
    void TearDown() override
    {
        GlobalMockObject::verify();
        expectedArgs = nullptr;
        expectedRecord = nullptr;
        expectedPrefix = nullptr;
        expectedPrefixSize = 0;
    }
};

bool RunInstrProbe(ProfDataCollect *, rtStream_t, const std::function<bool(void)> &launch)
{
    return launch();
}

template<typename Launch>
void PrepareExpandedProbeArgs(Launch &instance, rtArgsEx_t &original, uint8_t *record)
{
    instance.argsInfo_ = &original;
    instance.newArgsInfo_ = original;
    uint32_t offset = 0;
    ASSERT_TRUE(ExpandArgs(&instance.newArgsInfo_, instance.argsVec_, record, instance.hostInput_, offset));
    ASSERT_EQ(original.argsSize, 4U * sizeof(void *));
    ASSERT_EQ(instance.newArgsInfo_.argsSize, 5U * sizeof(void *));
    ASSERT_EQ(offset, original.argsSize);
    expectedArgs = &instance.newArgsInfo_;
    expectedRecord = record;
    instance.profObj_ = std::make_shared<ProfDataCollect>();
    // Both PC sampling and timeline use this callback. Select timeline here to
    // isolate argument forwarding from device address lookup and profiling I/O.
    MOCKER(&ProfDataCollect::IsPCSamplingNeedGen).stubs().will(returnValue(false));
    MOCKER(&ProfDataCollect::IsTimelineNeedGen).stubs().will(invoke(
        +[](ProfDataCollect *, ProfDBIType &type) {
            type = ProfDBIType::INSTR_PROF_END;
            return true;
        }));
    MOCKER(&ProfDataCollect::InstrProfData).expects(once()).will(invoke(RunInstrProbe));
    MOCKER(&Launch::PrepareDbiTask).expects(once()).will(returnValue(true));
    MOCKER(&Launch::ProfPre).stubs();
}
}

TEST_F(InstrProfLaunchArgs, flag_v2_forwards_expanded_args_without_modifying_original)
{
    int sentinels[4]{};
    void *inputs[4] = {&sentinels[0], &sentinels[1], &sentinels[2], &sentinels[3]};
    void *before[4];
    std::memcpy(before, inputs, sizeof(before));
    uint8_t record[8]{};
    rtArgsEx_t original{};
    original.args = inputs;
    original.argsSize = sizeof(inputs);
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    expectedPrefix = before;
    expectedPrefixSize = sizeof(before);
    PrepareExpandedProbeArgs(instance, original, record);
    MOCKER(rtKernelLaunchWithFlagV2Origin).expects(once())
        .will(invoke(+[](const void *, uint32_t, rtArgsEx_t *args, rtSmDesc_t *, rtStream_t,
                        uint32_t, const rtTaskCfgInfo_t *) { return CheckExpandedProbeArgs(args); }));
    instance.ProfPreForInstrProf([]() { return true; }, [](const std::string &) {}, nullptr);
    EXPECT_EQ(original.args, inputs);
    EXPECT_EQ(original.argsSize, sizeof(inputs));
    EXPECT_EQ(std::memcmp(inputs, before, sizeof(before)), 0);
}

TEST_F(InstrProfLaunchArgs, handle_v2_forwards_expanded_args_without_modifying_original)
{
    int sentinels[4]{};
    void *inputs[4] = {&sentinels[0], &sentinels[1], &sentinels[2], &sentinels[3]};
    void *before[4];
    std::memcpy(before, inputs, sizeof(before));
    uint8_t record[8]{};
    rtArgsEx_t original{};
    original.args = inputs;
    original.argsSize = sizeof(inputs);
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    expectedPrefix = before;
    expectedPrefixSize = sizeof(before);
    PrepareExpandedProbeArgs(instance, original, record);
    MOCKER(rtKernelLaunchWithHandleV2Origin).expects(once())
        .will(invoke(+[](void *, uint64_t, uint32_t, rtArgsEx_t *args, rtSmDesc_t *, rtStream_t,
                        const rtTaskCfgInfo_t *) { return CheckExpandedProbeArgs(args); }));
    instance.ProfPreForInstrProf([]() { return true; }, [](const std::string &) {}, nullptr);
    EXPECT_EQ(original.args, inputs);
    EXPECT_EQ(original.argsSize, sizeof(inputs));
    EXPECT_EQ(std::memcmp(inputs, before, sizeof(before)), 0);
}
