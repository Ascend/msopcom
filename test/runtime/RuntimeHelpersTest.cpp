/* -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
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
#include <memory>
#include <string>
#include <vector>
#include "runtime/inject_helpers/ArgsContext.h"
#define private public
#include "runtime/inject_helpers/KernelContext.h"
#undef private
#include "mockcpp/mockcpp.hpp"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "runtime/inject_helpers/StubFuncContext.h"
#include "runtime/inject_helpers/DbiRecordTaskHelper.h"
#include "stub/FakeElf.h"
#include "utils/ElfLoader.h"

namespace Helper = DbiRecordTaskHelper;

class RuntimeHelpersTest : public testing::Test {
protected:
    void SetUp() override { KernelContext::Instance().Reset(); }
    void TearDown() override
    {
        KernelContext::Instance().Reset();
        GlobalMockObject::verify();
    }

    std::vector<char> MakeBinary(bool scanKnown, bool hasSimt)
    {
        if (!scanKnown) {
            return {};
        }
        auto elf = CreateElfWithKernelSymbols({"kernel"},
            hasSimt ? std::vector<std::string>{"$t.0"} : std::vector<std::string>{});
        return {elf.begin(), elf.end()};
    }
};

TEST_F(RuntimeHelpersTest, stub_without_handle_has_no_pc_or_clone)
{
    auto reg = std::make_shared<RegisterContext>();
    StubFuncContext ctx(reg, "kernel", nullptr);
    MOCKER(aclrtGetFunctionAddrImplOrigin).expects(never());
    MOCKER(aclrtBinaryGetFunctionImplOrigin).expects(never());
    EXPECT_EQ(ctx.GetKernelPC(), 0U);
    EXPECT_EQ(ctx.GetStartPC(), 0U);
    EXPECT_EQ(ctx.Clone(reg), nullptr);
}

TEST_F(RuntimeHelpersTest, failed_clone_preserves_original_context)
{
    auto reg = std::make_shared<RegisterContext>();
    int function = 0;
    StubFuncContext ctx(reg, "kernel", &function);
    MOCKER(aclrtBinaryGetFunctionImplOrigin).expects(once()).will(returnValue(aclError(123)));
    EXPECT_EQ(ctx.Clone(std::make_shared<RegisterContext>()), nullptr);
    EXPECT_EQ(ctx.GetFuncHandle(), &function);
    EXPECT_EQ(ctx.GetRegisterContext(), reg);
    EXPECT_EQ(ctx.GetKernelName(), "kernel");
}

TEST_F(RuntimeHelpersTest, valid_function_handle_resolves_kernel_and_binary_start_pc)
{
    auto reg = std::make_shared<RegisterContext>();
    ASSERT_TRUE(reg->Init(MakeBinary(true, false), nullptr, RegisterParam{}));
    int function = 0;
    StubFuncContext ctx(reg, "kernel", &function);
    void *address = reinterpret_cast<void *>(0x1010U);
    void *receivedHandle = nullptr;
    MOCKER(aclrtGetFunctionAddrImplOrigin).expects(exactly(2))
        .with(spy(receivedHandle), outBoundP(&address), any()).will(returnValue(ACL_SUCCESS));
    EXPECT_EQ(ctx.GetKernelPC(), 0x1010U);
    EXPECT_EQ(receivedHandle, &function);
    // FakeElf puts the first kernel symbol at offset 0x10.
    EXPECT_EQ(ctx.GetStartPC(), 0x1000U);
    EXPECT_EQ(receivedHandle, &function);
}

TEST_F(RuntimeHelpersTest, clone_uses_target_binary_and_new_function_handle)
{
    auto source = std::make_shared<RegisterContext>();
    auto target = std::make_shared<RegisterContext>();
    int sourceFunc = 0, targetFunc = 0, sourceBinary = 0, targetBinary = 0;
    ASSERT_TRUE(source->Init(MakeBinary(true, false), &sourceBinary, RegisterParam{}));
    ASSERT_TRUE(target->Init(MakeBinary(true, false), &targetBinary, RegisterParam{}));
    StubFuncContext ctx(source, "kernel", &sourceFunc);
    void *receivedBinary = nullptr;
    void *clonedHandle = &targetFunc;
    const char *receivedName = nullptr;
    MOCKER(aclrtBinaryGetFunctionImplOrigin).expects(once())
        .with(spy(receivedBinary), spy(receivedName), outBoundP(&clonedHandle))
        .will(returnValue(ACL_SUCCESS));
    auto clone = ctx.Clone(target);
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &ctx);
    EXPECT_EQ(receivedBinary, &targetBinary);
    EXPECT_STREQ(receivedName, "kernel");
    EXPECT_EQ(clone->GetFuncHandle(), &targetFunc);
    EXPECT_EQ(clone->GetRegisterContext(), target);
    EXPECT_EQ(clone->GetKernelName(), "kernel");
    EXPECT_EQ(ctx.GetFuncHandle(), &sourceFunc);
    EXPECT_EQ(ctx.GetRegisterContext(), source);
}

TEST_F(RuntimeHelpersTest, unsupported_dbi_mode_requires_no_buffer_or_collection)
{
    const auto unknown = static_cast<ProfDBIType>(-1);
    MOCKER_CPP(&ProfDataCollect::GenDBIData).expects(never());
    MOCKER_CPP(&ProfDataCollect::GenRecordData).expects(never());
    EXPECT_FALSE(Helper::IsNeedGen(nullptr, unknown));
    EXPECT_EQ(Helper::GetDbiRecordMemSize(unknown, 32), 0U);
    Helper::CollectData(nullptr, unknown, 0, nullptr);
}

TEST_F(RuntimeHelpersTest, warp_buffer_size_uses_runtime_core_count)
{
    uint64_t blocks = 0;
    MOCKER(GetCoreNumForDbi).expects(once()).with(spy(blocks)).will(returnValue(uint64_t(3)));
    constexpr uint64_t blockBytes = sizeof(WarpHeader) + WARP_NUM_PER_BLOCK * sizeof(WarpRecord) + BLOCK_GAP;
    EXPECT_EQ(blockBytes * 3, 3312U); // (16 + 64 * 16 + 64) * 3, per the warp record protocol.
    EXPECT_EQ(Helper::GetDbiRecordMemSize(ProfDBIType::WARP_TIMELINE, 7), blockBytes * 3);
    EXPECT_EQ(blocks, 7U);
}

class DbiBufferSizeTest : public RuntimeHelpersTest, public testing::WithParamInterface<bool> {};

TEST_P(DbiBufferSizeTest, memory_chart_size_uses_chip_layout_and_runtime_core_count)
{
    MOCKER_CPP(&DeviceContext::IsAscend950).expects(once()).will(returnValue(GetParam()));
    uint64_t blocks = 0;
    MOCKER(GetCoreNumForDbi).expects(once()).with(spy(blocks)).will(returnValue(uint64_t(3)));
    const uint64_t blockBytes = (GetParam() ? 200ULL : 100ULL) * 1024 * 1024 + 64;
    EXPECT_EQ(Helper::GetDbiRecordMemSize(ProfDBIType::MEMORY_CHART, 7), blockBytes * 3);
    EXPECT_EQ(blocks, 7U);
}

TEST_P(DbiBufferSizeTest, explicit_memory_chart_core_count_skips_runtime_query)
{
    MOCKER_CPP(&DeviceContext::IsAscend950).expects(once()).will(returnValue(GetParam()));
    MOCKER(GetCoreNumForDbi).expects(never());
    const uint64_t blockBytes = (GetParam() ? 200ULL : 100ULL) * 1024 * 1024 + 64;
    EXPECT_EQ(Helper::GetDbiRecordMemSize(ProfDBIType::MEMORY_CHART, 7, 5), blockBytes * 5);
}

INSTANTIATE_TEST_SUITE_P(ChipLayouts, DbiBufferSizeTest, testing::Bool());

TEST_F(RuntimeHelpersTest, operand_buffer_size_includes_header_all_types_threads_and_gaps)
{
    uint64_t blocks = 0;
    MOCKER(GetCoreNumForDbi).expects(once()).with(spy(blocks)).will(returnValue(uint64_t(3)));
    constexpr uint64_t threadBytes = static_cast<uint32_t>(OperandType::END) * sizeof(OperandRecord) +
        SIMT_THREAD_GAP;
    constexpr uint64_t blockBytes = threadBytes * (MAX_THREAD_NUM + 1) + BLOCK_GAP;
    EXPECT_EQ(Helper::GetDbiRecordMemSize(ProfDBIType::OPERAND_RECORD, 7), sizeof(OperandHeader) + blockBytes * 3);
    EXPECT_EQ(blocks, 7U);
}

class DbiCollectionRoutingTest : public RuntimeHelpersTest,
                                 public testing::WithParamInterface<ProfDBIType> {};

TEST_P(DbiCollectionRoutingTest, generation_requirement_uses_matching_record_mode)
{
    ProfDataCollect collector(nullptr, false);
    for (bool needed : {false, true}) {
        MOCKER_CPP(&ProfDataCollect::IsMemoryChartNeedGen)
            .expects(exactly(GetParam() == ProfDBIType::MEMORY_CHART ? 1 : 0)).will(returnValue(needed));
        MOCKER_CPP(&ProfDataCollect::IsOperandRecordNeedGen)
            .expects(exactly(GetParam() == ProfDBIType::OPERAND_RECORD ? 1 : 0)).will(returnValue(needed));
        MOCKER_CPP(&ProfDataCollect::IsWarpTimelineNeedGen)
            .expects(exactly(GetParam() == ProfDBIType::WARP_TIMELINE ? 1 : 0)).will(returnValue(needed));
        EXPECT_EQ(Helper::IsNeedGen(&collector, GetParam()), needed);
        GlobalMockObject::verify();
    }
}

TEST_P(DbiCollectionRoutingTest, forwards_buffer_size_and_record_kind)
{
    ProfDataCollect collector(nullptr, false);
    uint8_t buffer[8]{};
    uint64_t receivedSize = 0;
    uint8_t *receivedBuffer = nullptr;
    std::string receivedKind;
    if (GetParam() == ProfDBIType::MEMORY_CHART) {
        MOCKER_CPP(&ProfDataCollect::GenDBIData).expects(once())
            .with(spy(receivedSize), spy(receivedBuffer));
        MOCKER_CPP(&ProfDataCollect::GenRecordData).expects(never());
    } else {
        MOCKER_CPP(&ProfDataCollect::GenDBIData).expects(never());
        MOCKER_CPP(&ProfDataCollect::GenRecordData).expects(once())
            .with(spy(receivedSize), spy(receivedBuffer), spy(receivedKind));
    }
    Helper::CollectData(&collector, GetParam(), sizeof(buffer), buffer);
    EXPECT_EQ(receivedSize, sizeof(buffer));
    EXPECT_EQ(receivedBuffer, buffer);
    if (GetParam() != ProfDBIType::MEMORY_CHART) {
        EXPECT_EQ(receivedKind, GetParam() == ProfDBIType::OPERAND_RECORD ? OPERAND_RECORD : WARP_TIMELINE);
    }
}

INSTANTIATE_TEST_SUITE_P(RecordModes, DbiCollectionRoutingTest, testing::Values(
    ProfDBIType::MEMORY_CHART, ProfDBIType::OPERAND_RECORD, ProfDBIType::WARP_TIMELINE));

TEST_F(RuntimeHelpersTest, non_memory_chart_modes_skip_layout_initialization)
{
    MOCKER_CPP(&ProfDataCollect::InitMemoryChartLayout).expects(never());
    EXPECT_TRUE(Helper::InitMemoryChartLayout(nullptr, ProfDBIType::OPERAND_RECORD,
        RegisterContextSP{}, nullptr, 0));
    EXPECT_TRUE(Helper::InitMemoryChartLayout(nullptr, ProfDBIType::WARP_TIMELINE,
        uint64_t(42), nullptr, 0));
}

struct DbiLayoutCase {
    bool scanKnown;
    bool hasSimt;
    bool result;
};

class DbiLayoutResultTest : public RuntimeHelpersTest,
                            public testing::WithParamInterface<DbiLayoutCase> {};

TEST_P(DbiLayoutResultTest, registered_binary_layout_forwards_scan_flags_and_result)
{
    auto reg = std::make_shared<RegisterContext>();
    const auto param = GetParam();
    ASSERT_EQ(reg->Init(MakeBinary(param.scanKnown, param.hasSimt), nullptr, RegisterParam{}), param.scanKnown);
    ProfDataCollect collector(nullptr, false);
    uint8_t buffer[8]{};
    uint8_t *receivedBuffer = nullptr;
    uint64_t receivedSize = 0;
    bool scanKnown = true, hasSimt = true;
    MOCKER_CPP(&ProfDataCollect::InitMemoryChartLayout).expects(once())
        .with(spy(receivedBuffer), spy(receivedSize), spy(scanKnown), spy(hasSimt))
        .will(returnValue(param.result));
    EXPECT_EQ(Helper::InitMemoryChartLayout(&collector, ProfDBIType::MEMORY_CHART,
        reg, buffer, sizeof(buffer)), param.result);
    EXPECT_EQ(receivedBuffer, buffer);
    EXPECT_EQ(receivedSize, sizeof(buffer));
    EXPECT_EQ(scanKnown, param.scanKnown);
    EXPECT_EQ(hasSimt, param.hasSimt);
}

TEST_P(DbiLayoutResultTest, runtime_registration_layout_forwards_scan_flags_and_result)
{
    const auto param = GetParam();
    auto elf = MakeBinary(param.scanKnown, param.hasSimt);
    int binaryHandle = 0;
    rtDevBinary_t binary{};
    binary.data = elf.data();
    binary.length = elf.size();
    auto &context = KernelContext::Instance();
    context.AddHdlRegisterEvent(&binaryHandle, &binary);
    const auto regId = context.GetRegisterId(&binaryHandle, true);
    ASSERT_NE(regId, UINT64_MAX);
    ProfDataCollect collector(nullptr, false);
    uint8_t buffer[8]{};
    uint8_t *receivedBuffer = nullptr;
    uint64_t receivedSize = 0;
    bool scanKnown = true, hasSimt = true;
    MOCKER_CPP(&ProfDataCollect::InitMemoryChartLayout).expects(once())
        .with(spy(receivedBuffer), spy(receivedSize), spy(scanKnown), spy(hasSimt))
        .will(returnValue(param.result));
    EXPECT_EQ(Helper::InitMemoryChartLayout(&collector, ProfDBIType::MEMORY_CHART,
        regId, buffer, sizeof(buffer)), param.result);
    EXPECT_EQ(receivedBuffer, buffer);
    EXPECT_EQ(receivedSize, sizeof(buffer));
    EXPECT_EQ(scanKnown, param.scanKnown);
    EXPECT_EQ(hasSimt, param.hasSimt);
}

INSTANTIATE_TEST_SUITE_P(ScanStatesAndResults, DbiLayoutResultTest, testing::Values(
    DbiLayoutCase{false, false, false}, DbiLayoutCase{false, false, true},
    DbiLayoutCase{true, false, false}, DbiLayoutCase{true, false, true},
    DbiLayoutCase{true, true, false}, DbiLayoutCase{true, true, true}));

TEST_F(RuntimeHelpersTest, readable_binary_scan_failure_preserves_unknown_layout_for_both_entries)
{
    auto elf = MakeBinary(true, false);
    Elf64_Ehdr header{};
    ASSERT_TRUE(ElfLoader::LoadHeader(elf, header));
    MOCKER(ScanKernelSymbols).expects(exactly(2)).will(returnValue(false));
    auto reg = std::make_shared<RegisterContext>();
    ASSERT_FALSE(reg->Init(elf, nullptr, RegisterParam{}));
    int binaryHandle = 0;
    rtDevBinary_t binary{};
    binary.data = elf.data();
    binary.length = elf.size();
    auto &context = KernelContext::Instance();
    context.AddHdlRegisterEvent(&binaryHandle, &binary);
    const auto regId = context.GetRegisterId(&binaryHandle, true);
    ASSERT_NE(regId, UINT64_MAX);
    ProfDataCollect collector(nullptr, false);
    uint8_t buffer[8]{};
    bool scanKnown = true, hasSimt = true;
    MOCKER_CPP(&ProfDataCollect::InitMemoryChartLayout).expects(exactly(2))
        .with(any(), any(), spy(scanKnown), spy(hasSimt)).will(returnValue(false));
    EXPECT_FALSE(Helper::InitMemoryChartLayout(&collector, ProfDBIType::MEMORY_CHART,
        reg, buffer, sizeof(buffer)));
    EXPECT_FALSE(scanKnown);
    EXPECT_FALSE(hasSimt);
    scanKnown = hasSimt = true;
    EXPECT_FALSE(Helper::InitMemoryChartLayout(&collector, ProfDBIType::MEMORY_CHART,
        regId, buffer, sizeof(buffer)));
    EXPECT_FALSE(scanKnown);
    EXPECT_FALSE(hasSimt);
}

TEST_F(RuntimeHelpersTest, missing_registration_uses_unknown_scan_flags)
{
    ProfDataCollect collector(nullptr, false);
    uint8_t buffer[8]{};
    uint8_t *receivedBuffer = nullptr;
    uint64_t receivedSize = 0;
    bool scanKnown = true, hasSimt = true;
    MOCKER_CPP(&ProfDataCollect::InitMemoryChartLayout).expects(exactly(2))
        .with(spy(receivedBuffer), spy(receivedSize), spy(scanKnown), spy(hasSimt))
        .will(returnValue(false)).then(returnValue(true));
    EXPECT_FALSE(Helper::InitMemoryChartLayout(&collector, ProfDBIType::MEMORY_CHART,
        RegisterContextSP{}, buffer, sizeof(buffer)));
    EXPECT_EQ(receivedBuffer, buffer);
    EXPECT_EQ(receivedSize, sizeof(buffer));
    EXPECT_FALSE(scanKnown);
    EXPECT_FALSE(hasSimt);
    scanKnown = hasSimt = true;
    EXPECT_TRUE(Helper::InitMemoryChartLayout(&collector, ProfDBIType::MEMORY_CHART,
        UINT64_MAX, buffer, sizeof(buffer)));
    EXPECT_EQ(receivedBuffer, buffer);
    EXPECT_EQ(receivedSize, sizeof(buffer));
    EXPECT_FALSE(scanKnown);
    EXPECT_FALSE(hasSimt);
}

TEST_F(RuntimeHelpersTest, runtime_failure_logs_identify_each_record_mode)
{
    EXPECT_STREQ(Helper::GetRtFailedLogPrefix(ProfDBIType::MEMORY_CHART),
        "Memory chart kernel launch failed");
    EXPECT_STREQ(Helper::GetRtFailedLogPrefix(ProfDBIType::OPERAND_RECORD),
        "Operand record kernel launch failed");
    EXPECT_STREQ(Helper::GetRtFailedLogPrefix(ProfDBIType::WARP_TIMELINE),
        "Warp timeline kernel launch failed");
    EXPECT_STREQ(Helper::GetRtFailedLogPrefix(static_cast<ProfDBIType>(-1)),
        "Dbi record kernel launch failed");
}

TEST_F(RuntimeHelpersTest, dfx_arguments_reference_output_directory_and_configured_pipe)
{
    MOCKER_CPP(&ProfConfig::GetInstrTimelinePipe).expects(once()).will(returnValue(std::string("pipe.sock")));
    std::vector<std::string> args = {"stale-option"};
    std::string tuneLog = "stale-log";
    Helper::AppendExtraInfo(ProfDBIType::INSTR_PROF_DFX, "/output", tuneLog, args);
    EXPECT_EQ(args, (std::vector<std::string>{"--dfx-region-instr-pipe=pipe.sock",
        "--dfx-region-id-map=/output/dfx_region_map.txt"}));
    EXPECT_EQ(tuneLog, "/output/dfx_tune.log");
}

TEST_F(RuntimeHelpersTest, unrelated_modes_preserve_existing_compiler_options)
{
    std::vector<std::string> args = {"--existing-option", "--append-hbmout-paraminfo"};
    const auto originalArgs = args;
    std::string tuneLog = "/existing/tune.log";
    Helper::AppendExtraInfo(ProfDBIType::OPERAND_RECORD, "/output", tuneLog, args);
    EXPECT_EQ(args, originalArgs);
    EXPECT_EQ(tuneLog, "/existing/tune.log");
}

TEST_F(RuntimeHelpersTest, start_mode_selects_start_stub_compiler_arguments)
{
    std::vector<std::string> args = {"--existing-option"};
    std::string tuneLog = "/existing/tune.log";
    Helper::AppendExtraInfo(ProfDBIType::INSTR_PROF_START, "/output", tuneLog, args);
    EXPECT_EQ(args, (std::vector<std::string>{START_STUB_COMPILER_ARGS}));
    EXPECT_EQ(tuneLog, "/existing/tune.log");
}

TEST_F(RuntimeHelpersTest, hbm_output_argument_is_appended_once_after_mode_arguments)
{
    std::vector<std::string> args = {"--existing-option"};
    Helper::AppendHbmOutParamInfoArgs(args);
    Helper::AppendHbmOutParamInfoArgs(args);
    EXPECT_EQ(args, (std::vector<std::string>{"--existing-option", APPEND_HBMOUT_PARAMINFO_ARGS}));
}

TEST_F(RuntimeHelpersTest, hbm_output_config_argument_preserves_existing_options_and_is_unique)
{
    auto &args = DBITaskConfig::Instance().extraCompilerArgs_;
    const auto previousArgs = args;
    args = {START_STUB_COMPILER_ARGS};
    Helper::AppendHbmOutParamInfoToConfig();
    Helper::AppendHbmOutParamInfoToConfig();
    EXPECT_EQ(args, (std::vector<std::string>{START_STUB_COMPILER_ARGS, APPEND_HBMOUT_PARAMINFO_ARGS}));
    args = previousArgs;
}
