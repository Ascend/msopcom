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
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <limits>
#include "mockcpp/mockcpp.hpp"
#include "core/FuncSelector.h"
#include "core/LocalProcess.h"
#include "utils/FileSystem.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "runtime/inject_helpers/ArgsContext.h"
#define private public
#include "runtime/inject_helpers/KernelContext.h"
#undef private
#include "runtime/HijackedFunc.h"
#include "runtime/inject_helpers/DBITask.h"
#include "runtime/inject_helpers/DevMemManager.h"
#include "runtime/inject_helpers/FusionLaunchArgs.h"
#include "runtime/inject_helpers/KernelMetaDataParser.h"
#include "runtime/inject_helpers/InstrReport.h"
#include "runtime/inject_helpers/LaunchManager.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/MemGuard.h"
#include "runtime/inject_helpers/SyncStreamWithInterrupt.h"

namespace {
struct FusionInput {
    rtLaunchAttribute_t attrs[2]{};
    rtLaunchConfig_t config{attrs, 2};
    rtFunsionTaskInfo_t tasks{};
    std::array<uint8_t, 64> bytes{};
    rtHostInputInfo_t inputs[2]{{16, 48}, {8, 33}};
    rtFusionArgsEx_t args{};
    FusionInput()
    {
        attrs[0].id = RT_LAUNCH_ATTRIBUTE_QOS;
        attrs[0].value.qos = 7;
        attrs[1].id = RT_LAUNCH_ATTRIBUTE_BLOCKDIM;
        attrs[1].value.blockDim = 4;
        tasks.subTaskNum = 2;
        tasks.subTask[0].type = RT_FUSION_AICORE;
        tasks.subTask[0].task.aicoreInfo = {reinterpret_cast<void *>(0xcc001), 52, &config, nullptr};
        tasks.subTask[1].type = RT_FUSION_CCU;
        auto &ccu = tasks.subTask[1].task.ccuInfo;
        ccu.taskNum = 1;
        ccu.ccuTaskInfo[0].missionId = 9;
        ccu.ccuTaskInfo[0].args[12] = 0x12345678;
        for (size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = i;
        }
        args.args = bytes.data();
        args.argsSize = bytes.size();
        args.hostInputInfoPtr = inputs;
        args.hostInputInfoNum = 2;
    }
};

class FusionHook : public HijackedFuncOfFusionLaunch {
public:
    explicit FusionHook(FuncType original) { originfunc_ = original; }
};

FusionInput *originalInput;
uint8_t record[64];
int calls;
bool expectInstrumented;
bool failDbi;
bool signalBound;
int guardFills;
int guardChecks;
int cleanupParts;
uint32_t metadataTilingParam;
bool expectRestoredContext;
bool sanitizerEnabled;
bool ascend950;
bool staticStub;
bool sectionHeadersOk;
bool skipKernel;
bool publishDbiBinary;
uint32_t metadataSkipNum;
uint32_t metadataInputCount;
uint64_t metadataTilingSize;
int dbiCalls;
std::vector<std::string> processingStages;

void ExpectRestoredContext()
{
    auto &context = KernelContext::Instance();
    EXPECT_EQ(context.GetArgsSize(), 88);
    EXPECT_EQ(context.GetKernelParamNum(), 11);
    EXPECT_EQ(context.GetSimtUbDynamicSize(), 123);
    EXPECT_EQ(context.GetOpMemInfo().tilingParamsNo, 0);
}
bool failInit;
bool clearMetadataDuringDbi;
rtError_t launchResult;
rtDevBinary_t registeredBinary;
uint64_t observedTiling;
uint32_t expectedDynUbufSize;

rtError_t Launch(void *tasks, rtStream_t stream, rtFusionArgsEx_t *args)
{
    ++calls;
    processingStages.push_back("launch");
    EXPECT_EQ(stream, reinterpret_cast<void *>(0xcc002));
    if (!expectInstrumented) {
        if (expectRestoredContext) { ExpectRestoredContext(); }
        EXPECT_EQ(tasks, &originalInput->tasks);
        EXPECT_EQ(args, &originalInput->args);
        return launchResult;
    }
    EXPECT_NE(tasks, &originalInput->tasks);
    auto &copy = *static_cast<rtFunsionTaskInfo_t *>(tasks);
    EXPECT_EQ(copy.subTaskNum, 2);
    EXPECT_EQ(std::memcmp(&copy.subTask[1], &originalInput->tasks.subTask[1], sizeof(rtFusionSubTaskInfo_t)), 0);
    EXPECT_EQ(copy.subTask[0].task.aicoreInfo.hdl, reinterpret_cast<void *>(0xcc003));
    EXPECT_EQ(copy.subTask[0].task.aicoreInfo.tilingKey, 52);
    EXPECT_EQ(copy.subTask[0].task.aicoreInfo.config->attrs[0].value.qos, 7);
    EXPECT_NE(args->args, originalInput->args.args);
    EXPECT_EQ(args->argsSize, 79);
    uint64_t recordPtr = 0;
    std::memcpy(&recordPtr, static_cast<uint8_t *>(args->args) + 40, sizeof(recordPtr));
    EXPECT_EQ(recordPtr, reinterpret_cast<uint64_t>(record));
    EXPECT_EQ(args->hostInputInfoPtr[0].dataOffset, 63);
    EXPECT_EQ(args->hostInputInfoPtr[1].dataOffset, 48);
    // Emulate runtime refilling the tiling pointer. Post-processing must read the owned copy.
    const uint64_t tiling = 0xabc000;
    std::memcpy(static_cast<uint8_t *>(args->args) + 16, &tiling, sizeof(tiling));
    return launchResult;
}

bool Dbi(void **hdl, uint64_t tilingKey)
{
    ++dbiCalls;
    EXPECT_EQ(*hdl, originalInput->tasks.subTask[0].task.aicoreInfo.hdl);
    EXPECT_EQ(tilingKey, 52);
    EXPECT_TRUE(signalBound);
    EXPECT_EQ(DBITaskConfig::Instance().argsSize_, 40);
    auto &context = KernelContext::Instance();
    rtDevBinary_t binary{};
    EXPECT_TRUE(context.GetDevBinary(KernelContext::KernelHandlePtr{*hdl}, binary));
    EXPECT_EQ(binary.length, registeredBinary.length);
    EXPECT_EQ(static_cast<const uint8_t *>(binary.data)[0], 42);
    *hdl = reinterpret_cast<void *>(0xcc003);
    if (publishDbiBinary) {
        context.SetDbiBinary(context.GetRegisterId(context.GetLaunchId()), registeredBinary);
    }
    if (clearMetadataDuringDbi) {
        context.ClearArgsInfo();
    }
    return !failDbi;
}
void Parse(KernelContext *, const rtDevBinary_t &, const rtArgsEx_t *)
{
    auto &info = KernelContext::Instance().GetOpMemInfo();
    info.Clear();
    info.tilingParamsNo = metadataTilingParam;
    info.skipNum = metadataSkipNum;
    info.tilingDataSize = metadataTilingSize;
    info.inputParamsAddrInfos.resize(metadataInputCount);
}
void MallocInfo(const rtArgsEx_t *args, KernelContext::OpMemInfo &info)
{
    processingStages.push_back("malloc-info");
    EXPECT_EQ(info.tilingParamsNo, 3);
    EXPECT_EQ(args->hasTiling, 1);
    EXPECT_EQ(args->tilingAddrOffset, 16);
    EXPECT_EQ(args->tilingDataOffset, 63);
    std::memcpy(&observedTiling, static_cast<uint8_t *>(args->args) + args->tilingAddrOffset, sizeof(observedTiling));
}
uint8_t *Init(uint64_t blockDim)
{
    EXPECT_EQ(blockDim, 4);
    EXPECT_EQ(KernelContext::Instance().GetSimtUbDynamicSize(), expectedDynUbufSize);
    return failInit ? nullptr : record;
}
aclError Device(int32_t *device)
{
    *device = 755;
    return ACL_ERROR_NONE;
}
} // namespace

TEST(FusionArgs, OwnsDescriptorsConfigAndUnsortedInlineArguments)
{
    FusionInput input;
    FusionLaunchArgs launch;
    ASSERT_TRUE(launch.Init(&input.tasks, &input.args));
    ASSERT_TRUE(launch.SetTiling(3));
    auto *address = launch.KernelArgs()->args;
    ASSERT_EQ(launch.HeadSize(), 33);
    ASSERT_EQ(launch.RecordOffset(), 40);
    ASSERT_EQ(launch.BlockDim(), 4);
    const auto originalBytes = input.bytes;
    input.attrs[1].value.blockDim = 99;
    input.inputs[0].dataOffset = 1;
    input.bytes.fill(0);
    input.tasks.subTask[0].task.aicoreInfo.hdl = nullptr;
    ASSERT_TRUE(launch.AppendRecord(record));
    EXPECT_EQ(launch.KernelArgs()->args, address);
    EXPECT_NE(launch.Aicore().hdl, nullptr);
    EXPECT_EQ(launch.Aicore().config->attrs[1].value.blockDim, 4);
    auto *copied = static_cast<uint8_t *>(launch.Args()->args);
    EXPECT_EQ(std::memcmp(copied, originalBytes.data(), 33), 0);
    EXPECT_EQ(std::memcmp(copied + 48, originalBytes.data() + 33, 31), 0);
    EXPECT_EQ(launch.Args()->hostInputInfoPtr[0].dataOffset, 63);
    EXPECT_EQ(launch.Args()->hostInputInfoPtr[1].dataOffset, 48);
    EXPECT_EQ(launch.KernelArgs()->tilingDataOffset, 63);
    EXPECT_FALSE(launch.AppendRecord(record));
}

TEST(FusionArgs, LocatesAicByTypeInEitherOrder)
{
    FusionInput input;
    std::swap(input.tasks.subTask[0], input.tasks.subTask[1]);
    FusionLaunchArgs launch;
    ASSERT_TRUE(launch.Init(&input.tasks, &input.args));
    EXPECT_EQ(launch.Aicore().tilingKey, 52);
    EXPECT_EQ(launch.Tasks()->subTask[0].type, RT_FUSION_CCU);
}

TEST(FusionArgs, ReadsDynamicUbByAttributeIdAndResetsWhenAbsent)
{
    FusionInput input;
    FusionLaunchArgs launch;
    for (uint32_t size : {0U, 16385U}) {
        input.attrs[0].id = RT_LAUNCH_ATTRIBUTE_DYN_UBUF_SIZE;
        input.attrs[0].value.dynUBufSize = size;
        ASSERT_TRUE(launch.Init(&input.tasks, &input.args));
        EXPECT_EQ(launch.DynUbufSize(), size);
        EXPECT_EQ(launch.Aicore().config->attrs[0].value.dynUBufSize, size);
        std::swap(input.attrs[0], input.attrs[1]);
        ASSERT_TRUE(launch.Init(&input.tasks, &input.args));
        EXPECT_EQ(launch.DynUbufSize(), size);
        EXPECT_EQ(launch.BlockDim(), 4);
        std::swap(input.attrs[0], input.attrs[1]);
    }
    input.attrs[0].id = RT_LAUNCH_ATTRIBUTE_QOS;
    input.attrs[0].value.qos = 7;
    ASSERT_TRUE(launch.Init(&input.tasks, &input.args));
    EXPECT_EQ(launch.DynUbufSize(), 0);
}

TEST(FusionArgs, AppendsAlignedRecordWithoutInlineInputs)
{
    FusionInput input;
    input.args.hostInputInfoNum = 0;
    input.args.hostInputInfoPtr = nullptr;
    input.args.argsSize = 31;
    FusionLaunchArgs launch;
    ASSERT_TRUE(launch.Init(&input.tasks, &input.args));
    EXPECT_EQ(launch.RecordOffset(), 32);
    ASSERT_TRUE(launch.AppendRecord(record));
    EXPECT_EQ(launch.Args()->argsSize, 40);
    EXPECT_EQ(std::memcmp(launch.Args()->args, input.bytes.data(), 31), 0);
}

TEST(FusionArgs, RejectsUnsupportedAndMalformedInputs)
{
    FusionInput input;
    FusionLaunchArgs launch;
    EXPECT_FALSE(launch.Init(nullptr, &input.args));
    input.args.isNoNeedH2DCopy = 1;
    EXPECT_FALSE(launch.Init(&input.tasks, &input.args));
    input.args.isNoNeedH2DCopy = 0;
    input.args.aicpuNum = 1;
    EXPECT_FALSE(launch.Init(&input.tasks, &input.args));
    input.args.aicpuNum = 0;
    input.inputs[0].addrOffset = 32;
    EXPECT_FALSE(launch.Init(&input.tasks, &input.args));
    input.inputs[0].addrOffset = 16;
    input.inputs[1].dataOffset = 65;
    EXPECT_FALSE(launch.Init(&input.tasks, &input.args));
    input.inputs[1].dataOffset = 33;
    input.args.argsSize = std::numeric_limits<uint32_t>::max();
    EXPECT_FALSE(launch.Init(&input.tasks, &input.args));
    input.args.argsSize = 64;
    input.tasks.subTaskNum = 3;
    EXPECT_FALSE(launch.Init(&input.tasks, &input.args));
    input.tasks.subTaskNum = 2;
    input.tasks.subTask[1].type = RT_FUSION_AICORE;
    EXPECT_FALSE(launch.Init(&input.tasks, &input.args));
}

struct Mc2FusionLayout {
    const char *name;
    bool allGather;
    bool quantized;
    bool gatherOutput;
};

class Mc2FusionArgs : public testing::TestWithParam<Mc2FusionLayout> {};

namespace {
// Encode DFX metadata for the kernel signature, including the hidden Hcom context.
// The two trailing slots reserved by opbase are not kernel signature parameters.
std::vector<uint8_t> Mc2SignatureMetadata(uint32_t tilingSlot)
{
    std::vector<uint8_t> metadata{4, 0, 0, 0};
    for (uint32_t slot = 0; slot <= tilingSlot; ++slot) {
        const auto type = slot == 0 ? DfxTensorType::MC2_CTX :
            (slot == tilingSlot ? DfxTensorType::TILING_DATA : DfxTensorType::GENERAL_TENSOR);
        const uint64_t size = slot == tilingSlot ? 24 : 128;
        metadata.insert(metadata.end(), {0, 5, 0, 2, 0, 0, 0, 0, 0, 1, 0, static_cast<uint8_t>(type)});
        for (int shift = 56; shift >= 0; shift -= 8) {
            metadata.push_back(static_cast<uint8_t>(size >> shift));
        }
    }
    const auto length = metadata.size() - 4;
    metadata[2] = static_cast<uint8_t>(length);
    metadata[3] = static_cast<uint8_t>(length >> 8);
    return metadata;
}
} // namespace

TEST_P(Mc2FusionArgs, PreservesHiddenContextOptionalTensorsAndRuntimeTilingRefill)
{
    const auto layout = GetParam();
    // Kernel order: context, six inputs, outputs, workspace, tiling. AllGather
    // has c/gatherOut/amax outputs; ReduceScatter has c/amax outputs.
    // opbase additionally reserves slots for overflow and control arguments.
    const uint32_t headSlots = layout.allGather ? 14 : 13;
    const uint32_t tilingSlot = headSlots - 3;
    const uint32_t headSize = headSlots * sizeof(uint64_t);
    std::vector<uint64_t> slots(headSlots, 0);
    for (uint32_t i = 0; i <= tilingSlot; ++i) {
        slots[i] = 0x100000 + i * 0x1000;
    }
    slots[3] = 0; // Optional bias.
    if (!layout.quantized) {
        slots[4] = 0;
        slots[5] = 0;
    }
    slots[6] = 0; // Optional quantScale.
    if (layout.allGather) {
        slots[8] = layout.gatherOutput ? 0x800000 : 0;
        slots[9] = 0; // Optional amax.
    } else {
        slots[8] = 0; // Optional amax.
    }
    const std::array<uint64_t, 3> tiling{0x1234, 0x5678, 0x9abc};
    std::vector<uint8_t> bytes(headSize + sizeof(tiling) + sizeof(rtHostInputInfo_t), 0);
    std::memcpy(bytes.data(), slots.data(), headSize);
    std::memcpy(bytes.data() + headSize, tiling.data(), sizeof(tiling));
    // Like opbase, keep the host-input descriptor after the inline tiling data.
    const rtHostInputInfo_t input{static_cast<uint32_t>(tilingSlot * sizeof(uint64_t)), headSize};
    std::memcpy(bytes.data() + headSize + sizeof(tiling), &input, sizeof(input));
    const auto original = bytes;
    FusionInput fusion;
    std::swap(fusion.tasks.subTask[0], fusion.tasks.subTask[1]);
    fusion.args.args = bytes.data();
    fusion.args.argsSize = bytes.size();
    fusion.args.hostInputInfoNum = 1;
    fusion.args.hostInputInfoPtr = reinterpret_cast<rtHostInputInfo_t *>(
        bytes.data() + headSize + sizeof(tiling));

    KernelMetaDataParser parser(Mc2SignatureMetadata(tilingSlot));
    ASSERT_TRUE(parser.Parse());
    const auto &info = parser.GetOpMemInfo();
    ASSERT_TRUE(info.hasMc2Ctx);
    ASSERT_EQ(info.inputParamsAddrInfos.size(), tilingSlot);
    ASSERT_EQ(info.tilingParamsNo, tilingSlot + 1);
    ASSERT_EQ(info.tilingDataSize, sizeof(tiling));

    FusionLaunchArgs launch;
    ASSERT_TRUE(launch.Init(&fusion.tasks, &fusion.args));
    ASSERT_TRUE(launch.SetTiling(info.tilingParamsNo));
    ASSERT_EQ(launch.RecordOffset(), headSize);
    auto *stable = launch.KernelArgs()->args;
    ASSERT_TRUE(launch.AppendRecord(record));
    EXPECT_EQ(launch.KernelArgs()->args, stable);
    EXPECT_EQ(launch.Args()->argsSize, original.size() + sizeof(uint64_t));
    EXPECT_EQ(std::memcmp(stable, slots.data(), headSize), 0);
    EXPECT_EQ(std::memcmp(static_cast<uint8_t *>(stable) + headSize + sizeof(uint64_t),
        tiling.data(), sizeof(tiling)), 0);
    EXPECT_EQ(std::memcmp(&launch.Tasks()->subTask[0], &fusion.tasks.subTask[0],
        sizeof(rtFusionSubTaskInfo_t)), 0);
    uint64_t recordAddress = 0;
    std::memcpy(&recordAddress, static_cast<uint8_t *>(stable) + headSize, sizeof(recordAddress));
    EXPECT_EQ(recordAddress, reinterpret_cast<uint64_t>(record));
    const auto *view = launch.KernelArgs();
    ASSERT_EQ(view->hasTiling, 1);
    EXPECT_EQ(view->tilingAddrOffset, input.addrOffset);
    EXPECT_EQ(view->tilingDataOffset, headSize + sizeof(uint64_t));
    EXPECT_EQ(launch.Args()->hostInputInfoPtr[0].addrOffset, input.addrOffset);
    EXPECT_EQ(launch.Args()->hostInputInfoPtr[0].dataOffset, view->tilingDataOffset);
    const uint64_t deviceTiling = 0xabc000;
    std::memcpy(static_cast<uint8_t *>(launch.Args()->args) + input.addrOffset,
        &deviceTiling, sizeof(deviceTiling));
    uint64_t refilled = 0;
    std::memcpy(&refilled, static_cast<const uint8_t *>(view->args) + view->tilingAddrOffset, sizeof(refilled));
    EXPECT_EQ(refilled, deviceTiling);
    EXPECT_EQ(bytes, original);
}

INSTANTIATE_TEST_SUITE_P(TransformerOperators, Mc2FusionArgs,
    testing::Values(Mc2FusionLayout{"AllGatherWithGatherOutput", true, true, true},
                    Mc2FusionLayout{"AllGatherUsingWorkspace", true, false, false},
                    Mc2FusionLayout{"ReduceScatterWithScales", false, true, false},
                    Mc2FusionLayout{"ReduceScatterWithoutScales", false, false, false}),
    [](const testing::TestParamInfo<Mc2FusionLayout> &info) { return info.param.name; });

class FusionLaunch : public testing::Test {
protected:
    FusionInput input;
    std::array<uint8_t, 64> binary{};
    bool savedBound = false;
    void SetUp() override
    {
        sanitizerEnabled = true;
        ascend950 = true;
        staticStub = false;
        sectionHeadersOk = true;
        skipKernel = false;
        publishDbiBinary = true;
        metadataSkipNum = 0;
        metadataInputCount = 0;
        metadataTilingSize = 0;
        dbiCalls = 0;
        originalInput = &input;
        calls = 0;
        processingStages.clear();
        observedTiling = 0;
        expectedDynUbufSize = 0;
        expectInstrumented = true;
        failDbi = false;
        signalBound = false;
        guardFills = 0;
        guardChecks = 0;
        metadataTilingParam = 3;
        expectRestoredContext = false;
        MOCKER(BindSigIntHandler).stubs().will(invoke(+[]() { signalBound = true; }));
        MOCKER(&MemoryGuard::FillAllMemGuard).stubs().will(invoke(+[](MemoryGuard *) { ++guardFills; }));
        MOCKER(&MemoryGuard::CheckAllMemGuard).stubs().will(invoke(+[](MemoryGuard *) {
            ++guardChecks;
            processingStages.push_back("guard-check");
        }));
        failInit = false;
        clearMetadataDuringDbi = false;
        binary[0] = 42;
        launchResult = RT_ERROR_NONE;
        MOCKER(IsSanitizer).stubs().will(invoke(+[]() { return sanitizerEnabled; }));
        MOCKER(IsOpProf).stubs().will(returnValue(false));
        MOCKER(&DeviceContext::IsAscend950).stubs().will(invoke(+[](DeviceContext *) { return ascend950; }));
        MOCKER(aclrtGetDeviceImplOrigin).stubs().will(invoke(Device));
        // 使用已有 UT Reset 接口隔离注册、下发历史和标量，不给产品增加测试专用 API。
        KernelContext::Instance().Reset();
        ASSERT_EQ(KernelContext::Instance().GetNextRegisterId(), 0);
        DevMemManager::Instance().SetMemoryInitFlag(false);
        savedBound = LaunchManager::GetOrCreateStreamInfo(reinterpret_cast<void *>(0xcc002)).binded;
        LaunchManager::GetOrCreateStreamInfo(reinterpret_cast<void *>(0xcc002)).binded = false;
        MOCKER(HasStaticStub, bool (*)(const rtDevBinary_t &)).stubs().will(invoke(
            +[](const rtDevBinary_t &) { return staticStub; }));
        MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
        MOCKER(GetSectionHeaders, bool (*)(const rtDevBinary_t &, std::map<std::string, Elf64_Shdr> &))
            .stubs()
            .will(invoke(+[](const rtDevBinary_t &, std::map<std::string, Elf64_Shdr> &) {
                return sectionHeadersOk;
            }));
        MOCKER(&KernelContext::ParseMetaDataFromBinary).stubs().will(invoke(Parse));
        MOCKER(SkipSanitizer).stubs().will(invoke(+[](const std::string &) { return skipKernel; }));
        MOCKER(RunDBITask, bool (*)(void **, uint64_t)).stubs().will(invoke(Dbi));
        MOCKER(__sanitizer_init).stubs().will(invoke(Init));
        MOCKER(ReportKernelSummary, void (*)(uint64_t)).stubs();
        MOCKER(ReportOpMallocInfo, void (*)(const rtArgsEx_t *, KernelContext::OpMemInfo &))
            .stubs()
            .will(invoke(MallocInfo));
        registeredBinary = {RT_DEV_BINARY_MAGIC_ELF_AICUBE, 0, binary.data(), binary.size()};
        KernelContext::Instance().AddHdlRegisterEvent(
            input.tasks.subTask[0].task.aicoreInfo.hdl, &registeredBinary, nullptr);
    }
    void TearDown() override
    {
        LaunchManager::GetOrCreateStreamInfo(reinterpret_cast<void *>(0xcc002)).binded = savedBound;
        KernelContext::Instance().Reset();
        EXPECT_FALSE(KernelContext::Instance().CheckHdlVaild(reinterpret_cast<void *>(0xcc001)));
        EXPECT_EQ(KernelContext::Instance().GetArgsSize(), 0);
        EXPECT_EQ(KernelContext::Instance().GetKernelParamNum(), 0);
        EXPECT_EQ(KernelContext::Instance().GetSimtUbDynamicSize(), 0);
        DevMemManager::Instance().SetMemoryInitFlag(false);
        GlobalMockObject::verify();
    }
    rtError_t Run()
    {
        FusionHook hook(Launch);
        return hook.Call(&input.tasks, reinterpret_cast<void *>(0xcc002), &input.args);
    }
};

TEST_F(FusionLaunch, SubmitsOneCompleteFusionAndUsesRuntimeRefilledTiling)
{
    MOCKER(SyncStreamWithInterrupt).expects(once()).will(returnValue(ACL_ERROR_NONE));
    MOCKER(__sanitizer_finalize).expects(once());
    MOCKER(&DevMemManager::Free).expects(once());
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(guardFills, 1);
    EXPECT_EQ(guardChecks, 1);
    EXPECT_EQ(observedTiling, 0xabc000);
    EXPECT_EQ(input.args.argsSize, 64);
    EXPECT_EQ(input.inputs[0].dataOffset, 48);
    EXPECT_EQ(input.tasks.subTask[0].task.aicoreInfo.hdl, reinterpret_cast<void *>(0xcc001));
}

TEST_F(FusionLaunch, InitializesRecordWithCurrentDynamicUbInsteadOfPreviousContext)
{
    MOCKER(SyncStreamWithInterrupt).expects(exactly(3)).will(returnValue(ACL_ERROR_NONE));
    MOCKER(__sanitizer_finalize).expects(exactly(3));
    MOCKER(&DevMemManager::Free).expects(exactly(3));
    rtLaunchAttribute_t attrs[3]{input.attrs[0], input.attrs[1], {}};
    attrs[2].id = RT_LAUNCH_ATTRIBUTE_DYN_UBUF_SIZE;
    input.config.attrs = attrs;
    input.config.numAttrs = 3;
    for (uint32_t size : {16385U, 0U}) {
        KernelContext::Instance().SetSimtUbDynamicSize(32768);
        expectedDynUbufSize = size;
        attrs[2].value.dynUBufSize = size;
        ASSERT_EQ(Run(), RT_ERROR_NONE);
        EXPECT_EQ(attrs[2].value.dynUBufSize, size);
    }
    input.config.numAttrs = 2;
    KernelContext::Instance().SetSimtUbDynamicSize(32768);
    expectedDynUbufSize = 0;
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_EQ(calls, 3);
}

TEST_F(FusionLaunch, PreservesLaunchMetadataWhenSharedContextIsClearedDuringDbi)
{
    clearMetadataDuringDbi = true;
    MOCKER(SyncStreamWithInterrupt).expects(once()).will(returnValue(ACL_ERROR_NONE));
    MOCKER(__sanitizer_finalize).expects(once());
    MOCKER(&DevMemManager::Free).expects(once());
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_EQ(observedTiling, 0xabc000);
    EXPECT_EQ(calls, 1);
}

TEST_F(FusionLaunch, FailedInstrumentationForwardsOriginalEvenIfDbiChangedHandle)
{
    failDbi = true;
    expectInstrumented = false;
    MOCKER(__sanitizer_finalize).expects(never());
    MOCKER(SyncStreamWithInterrupt).expects(never());
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_EQ(calls, 1);
}

TEST_F(FusionLaunch, LaunchErrorStillProcessesRecordsAndPreservesLaunchResult)
{
    launchResult = RT_ERROR_INVALID_VALUE;
    MOCKER(SyncStreamWithInterrupt).expects(once()).will(returnValue(ACL_ERROR_NONE));
    MOCKER(__sanitizer_finalize).expects(once());
    MOCKER(&DevMemManager::Free).expects(once());
    MOCKER(ExitAfterProcess).expects(once());
    EXPECT_EQ(Run(), launchResult);
    EXPECT_EQ(observedTiling, 0xabc000);
    EXPECT_EQ(calls, 1);
}

TEST_F(FusionLaunch, FailedSyncPreservesLaunchResultAndProcessesRecords)
{
    MOCKER(SyncStreamWithInterrupt).expects(once()).will(returnValue(ACL_ERROR_INTERNAL_ERROR));
    MOCKER(__sanitizer_finalize).expects(once());
    MOCKER(&DevMemManager::Free).expects(once());
    MOCKER(ExitAfterProcess).expects(once());
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_EQ(observedTiling, 0xabc000);
    EXPECT_EQ(calls, 1);
}

TEST_F(FusionLaunch, FailedLaunchAndSyncStillProcessRecordsAndPreserveLaunchError)
{
    launchResult = RT_ERROR_INVALID_VALUE;
    MOCKER(SyncStreamWithInterrupt).expects(once()).will(returnValue(ACL_ERROR_INTERNAL_ERROR));
    MOCKER(__sanitizer_finalize).expects(once());
    MOCKER(&DevMemManager::Free).expects(once());
    MOCKER(ExitAfterProcess).expects(once());
    EXPECT_EQ(Run(), launchResult);
    EXPECT_EQ(observedTiling, 0xabc000);
    EXPECT_EQ(calls, 1);
}

TEST_F(FusionLaunch, DeviceArgumentsAndSinkForwardOriginalOnce)
{
    expectInstrumented = false;
    input.args.isNoNeedH2DCopy = 1;
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    input.args.isNoNeedH2DCopy = 0;
    LaunchManager::GetOrCreateStreamInfo(reinterpret_cast<void *>(0xcc002)).binded = true;
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_EQ(calls, 2);
}

TEST_F(FusionLaunch, RegisterHookOwnsOriginalBinaryBytes)
{
    void *hdl = reinterpret_cast<void *>(0xcc004);
    binary[0] = 123;
    HijackedFuncOfRegisterAllKernel registration;
    registration.Pre(&registeredBinary, &hdl);
    ASSERT_EQ(registration.Post(RT_ERROR_NONE), RT_ERROR_NONE);
    binary.fill(0);
    rtDevBinary_t owned{};
    ASSERT_TRUE(KernelContext::Instance().GetDevBinary(KernelContext::KernelHandlePtr{hdl}, owned));
    EXPECT_NE(owned.data, binary.data());
    EXPECT_EQ(static_cast<const uint8_t *>(owned.data)[0], 123);
}

TEST_F(FusionLaunch, FailedRecordInitializationForwardsOriginal)
{
    failInit = true;
    expectInstrumented = false;
    MOCKER(__sanitizer_finalize).expects(never());
    MOCKER(SyncStreamWithInterrupt).expects(never());
    MOCKER(&DevMemManager::Free).expects(once());
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_EQ(calls, 1);
}

TEST_F(FusionLaunch, UnknownBinaryForwardsOriginal)
{
    input.tasks.subTask[0].task.aicoreInfo.hdl = reinterpret_cast<void *>(0xcc005);
    expectInstrumented = false;
    MOCKER(__sanitizer_finalize).expects(never());
    MOCKER(SyncStreamWithInterrupt).expects(never());
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_EQ(calls, 1);
}

TEST(FusionLaunchEntry, MissingRuntimeSymbolReturnsExistingReservedError)
{
    FusionHook hook(nullptr);
    EXPECT_EQ(hook.Call(nullptr, nullptr, nullptr), RT_ERROR_RESERVED);
}

TEST(FusionBuffer, FreedBufferIsReleasedAndNextLaunchAllocatesAgain)
{
    auto &memory = DevMemManager::Instance();
    memory.Free();
    MOCKER(aclrtGetDeviceImplOrigin).stubs().will(invoke(Device));
    MOCKER(aclrtMallocImplOrigin)
        .expects(exactly(2))
        .will(invoke(+[](void **ptr, size_t size, aclrtMemMallocPolicy) -> aclError {
            *ptr = std::malloc(size);
            return *ptr ? ACL_ERROR_NONE : ACL_ERROR_BAD_ALLOC;
        }));
    MOCKER(aclrtFreeImplOrigin).expects(exactly(2)).will(invoke(+[](void *ptr) -> aclError {
        std::free(ptr);
        return ACL_ERROR_NONE;
    }));
    void *first = nullptr;
    ASSERT_EQ(memory.MallocMemory(&first, 64), ACL_ERROR_NONE);
    memory.SetMemoryInitFlag(true);
    memory.Free();
    void *next = nullptr;
    ASSERT_EQ(memory.MallocMemory(&next, 64), ACL_ERROR_NONE);
    EXPECT_NE(next, nullptr);
    memory.Free();
    GlobalMockObject::verify();
}

TEST(FusionArgs, RejectsMissingTilingAndDuplicateDestinationsButAllowsSharedData)
{
    FusionInput input;
    FusionLaunchArgs launch;
    ASSERT_TRUE(launch.Init(&input.tasks, &input.args));
    EXPECT_TRUE(launch.SetTiling(0));
    EXPECT_FALSE(launch.SetTiling(1)); // 元数据指定槽位，但没有内联描述。
    EXPECT_FALSE(launch.SetTiling(5)); // 超出参数头。
    input.inputs[1].addrOffset = input.inputs[0].addrOffset;
    EXPECT_FALSE(launch.Init(&input.tasks, &input.args));
    input.inputs[1].addrOffset = 8;
    input.inputs[1].dataOffset = input.inputs[0].dataOffset;
    ASSERT_TRUE(launch.Init(&input.tasks, &input.args));
    EXPECT_TRUE(launch.SetTiling(2));
    EXPECT_TRUE(launch.SetTiling(3));
}

TEST(FusionArgs, RejectsInvalidConfigAndAttributes)
{
    FusionInput input;
    FusionLaunchArgs launch;
    input.tasks.subTask[0].task.aicoreInfo.config = nullptr;
    EXPECT_FALSE(launch.Init(&input.tasks, &input.args));
    input.tasks.subTask[0].task.aicoreInfo.config = &input.config;
    input.attrs[1].value.blockDim = 0;
    EXPECT_FALSE(launch.Init(&input.tasks, &input.args));
    input.attrs[1].value.blockDim = 4;
    input.attrs[0] = input.attrs[1];
    EXPECT_FALSE(launch.Init(&input.tasks, &input.args));
}

TEST_F(FusionLaunch, RestoresContextBeforeForwardAtEachPreparationFailure)
{
    auto &context = KernelContext::Instance();
    context.SetArgsSize(88);
    context.SetKernelParamNum(11);
    context.SetSimtUbDynamicSize(123);
    expectInstrumented = false;
    expectRestoredContext = true;
    MOCKER(__sanitizer_finalize).expects(never());
    MOCKER(SyncStreamWithInterrupt).expects(never());
    metadataTilingParam = 1;
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    metadataTilingParam = 3;
    failDbi = true;
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    failDbi = false;
    failInit = true;
    DevMemManager::Instance().SetMemoryInitFlag(true);
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_FALSE(DevMemManager::Instance().IsMemoryInit());
    EXPECT_EQ(guardFills, 0);
    EXPECT_EQ(guardChecks, 0);
    EXPECT_EQ(calls, 3);
}

TEST_F(FusionLaunch, RestoresTemporaryDbiConfigAfterFailedInstrumentation)
{
    auto &dbi = DBITaskConfig::Instance();
    const auto saved = dbi.argsSize_;
    dbi.argsSize_ = 712;
    failDbi = true;
    expectInstrumented = false;
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_EQ(dbi.argsSize_, 712);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(KernelContext::Instance().GetOpMemInfo().tilingParamsNo, 0);
    dbi.argsSize_ = saved;
}

TEST_F(FusionLaunch, CleansRecordsAndContextBeforeExitProcessing)
{
    auto &context = KernelContext::Instance();
    context.SetArgsSize(88);
    context.SetKernelParamNum(11);
    context.SetSimtUbDynamicSize(123);
    MOCKER(SyncStreamWithInterrupt).expects(once()).will(returnValue(ACL_ERROR_NONE));
    MOCKER(__sanitizer_finalize).expects(once());
    MOCKER(&DevMemManager::Free).expects(once());
    cleanupParts = 0;
    MOCKER(ReportSectionsFree).expects(once()).will(invoke(
        +[](uint64_t, const std::vector<Elf64_Shdr> &) { ++cleanupParts; }));
    MOCKER(ReportOverflowFree, void (*)(KernelContext::OpMemInfo &)).expects(once()).will(invoke(
        +[](KernelContext::OpMemInfo &) { ++cleanupParts; }));
    MOCKER(ReportOpFreeInfo, void (*)(KernelContext::OpMemInfo &)).expects(once()).will(invoke(
        +[](KernelContext::OpMemInfo &) { ++cleanupParts; }));
    MOCKER(ExitAfterProcess).expects(once()).will(invoke(+[]() {
        EXPECT_EQ(cleanupParts, 3);
        ExpectRestoredContext();
    }));
    EXPECT_EQ(Run(), RT_ERROR_NONE);
}

TEST_F(FusionLaunch, FailedRecordAppendResetsInitFlagAndForwardsWithRestoredContext)
{
    auto &context = KernelContext::Instance();
    context.SetArgsSize(88);
    context.SetKernelParamNum(11);
    context.SetSimtUbDynamicSize(123);
    expectInstrumented = false;
    expectRestoredContext = true;
    DevMemManager::Instance().SetMemoryInitFlag(true);
    MOCKER(&FusionLaunchArgs::AppendRecord).expects(once()).will(returnValue(false));
    MOCKER(&DevMemManager::Free).expects(once());
    MOCKER(SyncStreamWithInterrupt).expects(never());
    MOCKER(__sanitizer_finalize).expects(never());
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_FALSE(DevMemManager::Instance().IsMemoryInit());
    EXPECT_EQ(guardFills, 0);
    EXPECT_EQ(guardChecks, 0);
    EXPECT_EQ(calls, 1);
}

// 成功和错误返回都应按相同顺序处理已有记录，且 finalize 使用本次记录地址与 blockDim。
TEST_F(FusionLaunch, ProcessesRecordsInOrderForSuccessAndFailure)
{
    MOCKER(SyncStreamWithInterrupt).expects(exactly(4)).will(invoke(+[](rtStream_t stream) -> aclError {
        EXPECT_EQ(stream, reinterpret_cast<void *>(0xcc002));
        processingStages.push_back("sync");
        return calls % 2 == 0 ? ACL_ERROR_INTERNAL_ERROR : ACL_ERROR_NONE;
    }));
    MOCKER(__sanitizer_finalize).expects(exactly(4)).will(invoke(+[](uint8_t *ptr, uint64_t blockDim) {
        EXPECT_EQ(ptr, record);
        EXPECT_EQ(blockDim, 4);
        processingStages.push_back("finalize");
    }));
    MOCKER(&DevMemManager::Free).expects(exactly(4)).will(invoke(+[](DevMemManager *) {
        processingStages.push_back("free");
    }));
    const std::vector<std::string> expected{"launch", "sync", "guard-check", "malloc-info", "finalize", "free"};
    for (auto result : {RT_ERROR_NONE, RT_ERROR_INVALID_VALUE}) {
        launchResult = result;
        for (int repeat = 0; repeat < 2; ++repeat) {
            processingStages.clear();
            EXPECT_EQ(Run(), result);
            EXPECT_EQ(processingStages, expected);
        }
    }
}

TEST_F(FusionLaunch, RestoresTemporaryDbiConfigAfterSuccessfulInstrumentation)
{
    auto &dbi = DBITaskConfig::Instance();
    const auto saved = dbi.argsSize_;
    dbi.argsSize_ = 712;
    MOCKER(SyncStreamWithInterrupt).expects(once()).will(returnValue(ACL_ERROR_NONE));
    MOCKER(__sanitizer_finalize).expects(once());
    MOCKER(&DevMemManager::Free).expects(once());
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_EQ(dbi.argsSize_, 712);
    EXPECT_EQ(calls, 1);
    dbi.argsSize_ = saved;
}

// 入口放行不建立插桩或记录区，即使原始 Runtime 返回错误也必须原样保留。
TEST_F(FusionLaunch, UnsupportedModeChipAndStaticBinaryForwardOriginal)
{
    expectInstrumented = false;
    launchResult = RT_ERROR_INVALID_VALUE;
    MOCKER(__sanitizer_init).expects(never());
    MOCKER(SyncStreamWithInterrupt).expects(never());
    MOCKER(__sanitizer_finalize).expects(never());
    sanitizerEnabled = false;
    EXPECT_EQ(Run(), launchResult);
    sanitizerEnabled = true;
    ascend950 = false;
    EXPECT_EQ(Run(), launchResult);
    ascend950 = true;
    staticStub = true;
    EXPECT_EQ(Run(), launchResult);
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(dbiCalls, 0);
    EXPECT_EQ(guardFills, 0);
    EXPECT_EQ(guardChecks, 0);
}

TEST_F(FusionLaunch, InvalidMetadataAndSkippedKernelForwardBeforeInstrumentation)
{
    expectInstrumented = false;
    MOCKER(__sanitizer_init).expects(never());
    MOCKER(SyncStreamWithInterrupt).expects(never());
    MOCKER(__sanitizer_finalize).expects(never());
    metadataSkipNum = 6;
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    metadataSkipNum = 3;
    metadataInputCount = 2;
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    metadataSkipNum = 0;
    metadataInputCount = 0;
    metadataTilingSize = 0x1000;
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    metadataTilingSize = 0;
    skipKernel = true;
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_EQ(calls, 4);
    EXPECT_EQ(dbiCalls, 0);
    EXPECT_EQ(guardFills, 0);
    EXPECT_EQ(guardChecks, 0);
}

TEST_F(FusionLaunch, MissingLaunchEventAfterInstrumentationForwardsOriginal)
{
    expectInstrumented = false;
    MOCKER(&KernelContext::GetLaunchEvent).stubs().will(returnValue(false));
    MOCKER(__sanitizer_init).expects(never());
    MOCKER(SyncStreamWithInterrupt).expects(never());
    MOCKER(__sanitizer_finalize).expects(never());
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(dbiCalls, 1);
}

TEST_F(FusionLaunch, MissingInstrumentedBinaryOrSectionsForwardsBeforeRecordAllocation)
{
    expectInstrumented = false;
    MOCKER(__sanitizer_init).expects(never());
    MOCKER(SyncStreamWithInterrupt).expects(never());
    MOCKER(__sanitizer_finalize).expects(never());
    publishDbiBinary = false;
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    publishDbiBinary = true;
    sectionHeadersOk = false;
    EXPECT_EQ(Run(), RT_ERROR_NONE);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(dbiCalls, 2);
    EXPECT_EQ(guardFills, 0);
    EXPECT_EQ(guardChecks, 0);
}

namespace {
std::vector<uint32_t> convertedLengths;
std::map<KernelReplaceTask *, std::pair<void *, uint32_t>> convertedHandles;

bool CaptureConversion(DBITask *, const BinaryInstrumentation::Config &config,
    const std::string &, const std::string &, const std::string &)
{
    convertedLengths.push_back(config.argSize);
    return true;
}
bool RegisterConverted(KernelReplaceTask *task, void **handle, uint64_t, bool)
{
    *handle = reinterpret_cast<void *>(0xcd000 + convertedLengths.size() * 0x100);
    convertedHandles[task] = {*handle, convertedLengths.back()};
    return true;
}
} // namespace

// 保留真实 RunDBITask、DBITaskFactory 和 DBITask，仅替换文件操作、二进制转换与设备注册。
class FusionDbiCache : public testing::Test {
protected:
    void SetUp() override
    {
        convertedLengths.clear();
        convertedHandles.clear();
        MOCKER(aclrtGetDeviceImplOrigin).stubs().will(invoke(Device));
        KernelContext::Instance().Reset();
        DBITaskFactory::Instance().Reset();
        DBITaskConfig::Instance().Reset();
        DBITaskConfig::Instance().Init(BIType::CUSTOMIZE, "fusion-cache-test");
        MOCKER(&DeviceContext::GetSocVersion).stubs().will(returnValue(std::string("Ascend950")));
        MOCKER(HasStaticStub, bool (*)(const rtDevBinary_t &)).stubs().will(returnValue(false));
        MOCKER(MkdirRecusively).stubs().will(returnValue(true));
        MOCKER(RemoveAll).stubs();
        MOCKER(&KernelContext::DumpKernelObject, bool (KernelContext::*)(uint64_t, const std::string &))
            .stubs().will(returnValue(true));
        MOCKER(&DBITask::Convert).stubs().will(invoke(CaptureConversion));
        MOCKER(&KernelReplaceTask::Run, bool (KernelReplaceTask::*)(void **, uint64_t, bool))
            .stubs().will(invoke(RegisterConverted));
        MOCKER(&KernelReplaceTask::GetHandle).stubs().will(invoke(+[](KernelReplaceTask *task) {
            return convertedHandles.at(task).first;
        }));
        using UpdatePc = bool (KernelContext::DeviceContext::*)(uint64_t, const KernelContext::KernelHandleArgs &);
        MOCKER(&KernelContext::DeviceContext::UpdatePcStartAddrByDbi<KernelContext::KernelHandleArgs>, UpdatePc)
            .stubs().will(returnValue(true));
    }
    void TearDown() override
    {
        DBITaskFactory::Instance().Reset();
        DBITaskConfig::Instance().Reset();
        KernelContext::Instance().Reset();
        GlobalMockObject::verify();
    }
};

namespace {
void VerifyFusionCacheLayouts(bool withOrdinaryLaunch)
{
    FusionInput input;
    std::array<uint8_t, 64> binary{};
    rtDevBinary_t devBinary{RT_DEV_BINARY_MAGIC_ELF_AICUBE, 0, binary.data(), binary.size()};
    auto &context = KernelContext::Instance();
    void *original = input.tasks.subTask[0].task.aicoreInfo.hdl;
    context.AddHdlRegisterEvent(original, &devBinary);
    auto &config = DBITaskConfig::Instance();
    // 可先模拟同一句柄、tilingKey 的普通下发，再切换到两种融合参数头布局。
    void *ordinaryHandle = nullptr;
    if (withOrdinaryLaunch) {
        config.argsSize_ = 16;
        context.AddLaunchEvent(original, 52, 4, nullptr, nullptr);
        ordinaryHandle = original;
        ASSERT_TRUE(RunDBITask(&ordinaryHandle, 52));
    }
    void *firstFusionHandle = nullptr;
    for (uint32_t headSize : {24U, 24U, 32U, 24U}) {
        input.inputs[0] = {16, headSize};
        input.args.hostInputInfoNum = 1;
        input.args.argsSize = headSize + 16;
        FusionLaunchArgs launch;
        ASSERT_TRUE(launch.Init(&input.tasks, &input.args));
        ASSERT_TRUE(launch.SetTiling(3));
        config.argsSize_ = launch.RecordOffset();
        context.AddLaunchEvent(original, 52, 4, launch.KernelArgs(), nullptr);
        ASSERT_TRUE(RunDBITask(&launch.Aicore().hdl, 52));
        ASSERT_TRUE(launch.AppendRecord(record));
        const auto handle = launch.Aicore().hdl;
        uint32_t actualOffset = UINT32_MAX;
        for (const auto &entry : convertedHandles) {
            if (entry.second.first == handle) { actualOffset = entry.second.second; }
        }
        ASSERT_EQ(actualOffset, launch.RecordOffset());
        uint64_t recordAddress = 0;
        std::memcpy(&recordAddress, static_cast<const uint8_t *>(launch.Args()->args) + actualOffset,
            sizeof(recordAddress));
        EXPECT_EQ(recordAddress, reinterpret_cast<uint64_t>(record));
        EXPECT_NE(handle, ordinaryHandle);
        if (headSize == 24) {
            if (!firstFusionHandle) { firstFusionHandle = handle; }
            EXPECT_EQ(handle, firstFusionHandle);
        }
    }
    const std::vector<uint32_t> expected = withOrdinaryLaunch ?
        std::vector<uint32_t>{16, 24, 32} : std::vector<uint32_t>{24, 32};
    EXPECT_EQ(convertedLengths, expected);
}
} // namespace

TEST_F(FusionDbiCache, OrdinaryAndFusionLayoutsUseMatchingConvertedOffsetsAndReuseEqualLengths)
{
    VerifyFusionCacheLayouts(true);
}

TEST_F(FusionDbiCache, FusionOnlyLayoutsUseMatchingConvertedOffsetsAndReuseEqualLengths)
{
    VerifyFusionCacheLayouts(false);
}
