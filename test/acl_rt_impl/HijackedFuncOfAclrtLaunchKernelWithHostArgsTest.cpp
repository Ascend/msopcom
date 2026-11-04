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
#include <cstdlib>
#include "mockcpp/mockcpp.hpp"
#define private public
#define protected public
#include "runtime/inject_helpers/ProfConfig.h"
#include "acl_rt_impl/HijackedFunc.h"
#undef private
#undef protected
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "injectors/ContextMockHelper.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ArgsRawContext.h"
#include "runtime/inject_helpers/InstrReport.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/SyncStreamWithInterrupt.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/DBITask.h"
using namespace std;

namespace {
struct RecordArgProbe {
    uint32_t paramCountCalls = 0;
    uint32_t paramInfoCalls = 0;
    void Reset()
    {
        paramCountCalls = 0;
        paramInfoCalls = 0;
    }
};

RecordArgProbe g_recordArgProbe;
uint8_t g_memInfo[8] = {0};

// kernel 声明参数区 72 字节：64 字节用户 ABI + 8 字节 sanitizer 隐式参数
constexpr uint32_t DECLARED_ARG_SIZE = 72;
constexpr uint32_t MEM_INFO_OFFSET = 64;

using GetSectionHeadersVecFn = bool (*)(std::vector<char> const &, std::map<std::string, Elf64_Shdr> &);

bool GetSectionHeadersWithArgSizeStub(std::vector<char> const &binary, std::map<std::string, Elf64_Shdr> &headers)
{
    (void)binary;
    Elf64_Shdr argSizeHeader{};
    argSizeHeader.sh_offset = 0;
    argSizeHeader.sh_size = sizeof(uint32_t);
    headers["__CCE_KernelArgSize"] = argSizeHeader;

    Elf64_Shdr textHeader{};
    textHeader.sh_flags = SHF_ALLOC;
    headers[".text"] = textHeader;
    return true;
}

aclError GetParamCountOkStub(const void *func, size_t *paramCount)
{
    (void)func;
    ++g_recordArgProbe.paramCountCalls;
    *paramCount = 9; // 8 个用户参数 + 1 个 sanitizer 隐式参数
    return ACL_SUCCESS;
}

aclError GetParamCountFailStub(const void *func, size_t *paramCount)
{
    (void)func;
    (void)paramCount;
    ++g_recordArgProbe.paramCountCalls;
    return ACL_ERROR_BAD_ALLOC;
}

aclError GetParamInfoStub(const void *func, size_t paramIndex, size_t *paramOffset, size_t *paramSize)
{
    (void)func;
    (void)paramIndex;
    ++g_recordArgProbe.paramInfoCalls;
    *paramOffset = MEM_INFO_OFFSET;
    *paramSize = sizeof(uintptr_t);
    return ACL_SUCCESS;
}

uint32_t g_sectionsFreeCalls = 0;
uint32_t g_opFreeCalls = 0;
uint32_t g_dbiArgsSizeSeen = 0;

// 记录 Pre 异常早退时是否成对归还了内存
void SectionsFreeSpyStub(uint64_t, const std::vector<Elf64_Shdr> &) { ++g_sectionsFreeCalls; }
void OpFreeSpyStub(KernelContext::OpMemInfo &) { ++g_opFreeCalls; }
// 记录 RunDBITask 真正看到的 argsSize（动态插桩的 --tune-argsize 来源）
FuncContextSP RunDBITaskSpyStub(const LaunchContextSP &)
{
    g_dbiArgsSizeSeen = DBITaskConfig::Instance().argsSize_;
    return FuncContextSP{};
}
} // namespace

class HijackedFuncOfAclrtLaunchKernelWithHostArgsTest : public ContextMockHelper {
protected:
    void MockSanitizerPreCommon(bool initOk = true)
    {
        MOCKER(&BindSigIntHandler).stubs();
        MOCKER(&SkipSanitizer).stubs().will(returnValue(false));
        MOCKER(&ReportKernelSummary, void(*)(LaunchContextSP)).stubs();
        MOCKER(&aclrtGetDeviceImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
        MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
        MOCKER(&aclrtMemcpyImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
        MOCKER(&RunDBITask, FuncContextSP(*)(const LaunchContextSP &)).stubs().will(invoke(RunDBITaskSpyStub));
        MOCKER(&ReportOpMallocInfo, void(*)(const AclrtLaunchArgsInfo &, OpMemInfo &)).stubs();
        MOCKER(&ReportSectionsMalloc).stubs();
        GetSectionHeadersVecFn getSectionHeadersFn = &GetSectionHeaders;
        MOCKER(getSectionHeadersFn).stubs().will(invoke(GetSectionHeadersWithArgSizeStub));
        MOCKER(__sanitizer_init).stubs().will(returnValue(initOk ? &g_memInfo[0] : nullptr));
        // Pre 提前返回时也要成对回收已上报的内存，这里用间谍桩统计回收次数
        using SectionsFreeFn = void (*)(uint64_t, const std::vector<Elf64_Shdr> &);
        SectionsFreeFn sectionsFreeFn = &ReportSectionsFree;
        MOCKER(sectionsFreeFn).stubs().will(invoke(SectionsFreeSpyStub));
        using OpFreeFn = void (*)(KernelContext::OpMemInfo &);
        OpFreeFn opFreeFn = &ReportOpFreeInfo;
        MOCKER(opFreeFn).stubs().will(invoke(OpFreeSpyStub));
    }

    // 静态插桩 kernel：ELF 里声明 72 字节参数区
    void MockStaticStubKernel()
    {
        regCtx_->hasStaticStub_ = true;
        regCtx_->elfData_ = {0x48, 0x00, 0x00, 0x00}; // 小端 72
    }

    uint64_t RecordArgAt(const HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl &inst, size_t index)
    {
        auto argsCtx = std::static_pointer_cast<ArgsRawContext>(inst.launchCtx_->GetArgsContext());
        if (argsCtx == nullptr || argsCtx->GetArgs() == nullptr) {
            return 0;
        }
        return static_cast<uint64_t *>(argsCtx->GetArgs())[index];
    }
};

TEST_F(HijackedFuncOfAclrtLaunchKernelWithHostArgsTest, mock_valid_hijacked_input_then_test_call_expect_ok)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    aclrtStream stream = &placeholder_;
    aclrtLaunchKernelCfg cfg{};
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl inst;
    uint32_t aaa = 10;
    aclrtPlaceHolderInfo placeHolderArray{10, 10};
    ASSERT_EQ(inst.Call(funcHandle_, 3, stream, &cfg, &aaa, sizeof(aaa), &placeHolderArray, 1), ACL_SUCCESS);
}

TEST_F(HijackedFuncOfAclrtLaunchKernelWithHostArgsTest, input_nullptr_then_test_call_expect_no_core_dump)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl inst;
    testing::internal::CaptureStdout();
    ASSERT_EQ(inst.Call(nullptr, 3, nullptr, nullptr, nullptr, 0, nullptr, 0), ACL_SUCCESS);
    string capture = testing::internal::GetCapturedStdout();
    ASSERT_EQ(capture.find("ERROR"), std::string::npos);
}

TEST_F(HijackedFuncOfAclrtLaunchKernelWithHostArgsTest, call_function_msprof_simulator_init)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    std::string r;
    MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(r));
    ProfConfig::Instance().profConfig_.isSimulator = true;
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl inst;
    inst.profObj_ = std::make_shared<ProfDataCollect>(nullptr);
    inst.Pre(nullptr, 3, nullptr, nullptr, nullptr, 0, nullptr, 0);
    EXPECT_TRUE(inst.Post(ACL_SUCCESS) == ACL_SUCCESS);
}

TEST_F(HijackedFuncOfAclrtLaunchKernelWithHostArgsTest, prof_gen_bbfile_and_dbifile_fail)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    MOCKER(&ProfDataCollect::IsBBCountNeedGen).stubs().will(returnValue(true));
    MOCKER(&ProfDataCollect::IsMemoryChartNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, FuncContextSP(*)(const LaunchContextSP &)).stubs().will(returnValue(false));
    aclrtStream stream = &placeholder_;
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl inst;
    aclrtLaunchKernelCfg cfg{};
    uint32_t aaa = 10;
    aclrtPlaceHolderInfo placeHolderArray{10, 10};
    inst.profObj_ = std::make_shared<ProfDataCollect>(nullptr);
    inst.Pre(funcHandle_, 3, stream, &cfg, &aaa, sizeof(aaa), &placeHolderArray, 1);
    EXPECT_TRUE(inst.Post(ACL_SUCCESS) == ACL_SUCCESS);
}

TEST_F(HijackedFuncOfAclrtLaunchKernelWithHostArgsTest, test_operand_record_expand_args_failed)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    ProfConfig::Instance().profConfig_.isSimulator = false;
    MOCKER(&ProfDataCollect::IsOperandRecordNeedGen).stubs().will(returnValue(true));
    MOCKER(&RunDBITask, FuncContextSP(*)(const LaunchContextSP &)).stubs().will(returnValue(false));
    uint8_t* testBuffer = nullptr;
    MOCKER(&InitMemory, uint8_t*(*)(uint64_t)).stubs().will(returnValue(testBuffer));
    MOCKER(&rtGetL2CacheOffsetOrigin).stubs().will(returnValue(ACL_SUCCESS));
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl inst;
    aclrtLaunchKernelCfg cfg{};
    uint32_t aaa = 10;
    aclrtPlaceHolderInfo placeHolderArray{10, 10};
    auto func = []() -> void {};
    inst.refreshParamFunc_ = func;
    aclrtStream stream = &placeholder_;
    inst.profObj_ = std::make_shared<ProfDataCollect>(nullptr);
    inst.Pre(funcHandle_, 3, stream, &cfg, &aaa, sizeof(aaa), &placeHolderArray, 1);
    testing::internal::CaptureStdout();
    inst.ProfPost();
    string capture = testing::internal::GetCapturedStdout();
    ASSERT_TRUE(capture.find("ExpandArgs failed"));
    GlobalMockObject::verify();
}

/**
 * |  用例集  | HijackedFuncOfAclrtLaunchKernelWithHostArgsTest
 * | 测试函数 | SanitizerPre
 * |  用例名  | sanitizer_pre_fill_record_arg_by_param_table
 * | 用例描述 | 静态插桩 kernel，runtime 参数表可用时，按最后一个参数（sanitizer 隐式参数）的真实
 * |          | offset 写入记录区指针，且不改变调用方传入的 argsSize。
 */
TEST_F(HijackedFuncOfAclrtLaunchKernelWithHostArgsTest, sanitizer_pre_fill_record_arg_by_param_table)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    MockSanitizerPreCommon();
    MockStaticStubKernel();
    g_recordArgProbe.Reset();
    MOCKER(aclrtFunctionGetParamCountImplOrigin).stubs().will(invoke(GetParamCountOkStub));
    MOCKER(aclrtFunctionGetParamInfoImplOrigin).stubs().will(invoke(GetParamInfoStub));

    std::vector<uint8_t> hostArgs(DECLARED_ARG_SIZE, 0);
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl inst;
    aclrtLaunchKernelCfg cfg{};
    inst.Pre(funcHandle_, 3, &placeholder_, &cfg, hostArgs.data(), hostArgs.size(), nullptr, 0);

    ASSERT_EQ(g_recordArgProbe.paramCountCalls, 1U);
    ASSERT_EQ(g_recordArgProbe.paramInfoCalls, 1U);
    ASSERT_EQ(RecordArgAt(inst, MEM_INFO_OFFSET / sizeof(uintptr_t)), reinterpret_cast<uint64_t>(g_memInfo));
    ASSERT_EQ(inst.argsSize_, DECLARED_ARG_SIZE);
}

/**
 * |  用例集  | HijackedFuncOfAclrtLaunchKernelWithHostArgsTest
 * | 测试函数 | SanitizerPre
 * |  用例名  | sanitizer_pre_fill_record_arg_fallback_to_kernel_arg_size
 * | 用例描述 | runtime 参数表不可用（如 triton 的裸 .ascend.meta）时，退化为按
 * |          | __CCE_KernelArgSize 写入末尾 8 字节槽，且不触发参数追加。
 */
TEST_F(HijackedFuncOfAclrtLaunchKernelWithHostArgsTest, sanitizer_pre_fill_record_arg_fallback_to_kernel_arg_size)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    MockSanitizerPreCommon();
    MockStaticStubKernel();
    g_recordArgProbe.Reset();
    MOCKER(aclrtFunctionGetParamCountImplOrigin).stubs().will(invoke(GetParamCountFailStub));

    std::vector<uint8_t> hostArgs(DECLARED_ARG_SIZE, 0);
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl inst;
    aclrtLaunchKernelCfg cfg{};
    inst.Pre(funcHandle_, 3, &placeholder_, &cfg, hostArgs.data(), hostArgs.size(), nullptr, 0);

    ASSERT_EQ(g_recordArgProbe.paramCountCalls, 1U);
    ASSERT_EQ(g_recordArgProbe.paramInfoCalls, 0U);
    ASSERT_EQ(RecordArgAt(inst, MEM_INFO_OFFSET / sizeof(uintptr_t)), reinterpret_cast<uint64_t>(g_memInfo));
    ASSERT_EQ(inst.argsSize_, DECLARED_ARG_SIZE);
}

/**
 * |  用例集  | HijackedFuncOfAclrtLaunchKernelWithHostArgsTest
 * | 测试函数 | SanitizerPre
 * |  用例名  | sanitizer_pre_expand_args_when_caller_args_too_short
 * | 用例描述 | 调用方只传 64 字节（未预留隐式参数槽，3.2.2 场景）时，填槽条件不成立，
 * |          | 回退为尾部追加，argsSize 由 64 变为 72。
 */
TEST_F(HijackedFuncOfAclrtLaunchKernelWithHostArgsTest, sanitizer_pre_expand_args_when_caller_args_too_short)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    MockSanitizerPreCommon();
    MockStaticStubKernel();
    g_recordArgProbe.Reset();
    MOCKER(aclrtFunctionGetParamCountImplOrigin).stubs().will(invoke(GetParamCountFailStub));

    std::vector<uint8_t> hostArgs(MEM_INFO_OFFSET, 0);
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl inst;
    aclrtLaunchKernelCfg cfg{};
    inst.Pre(funcHandle_, 3, &placeholder_, &cfg, hostArgs.data(), hostArgs.size(), nullptr, 0);

    ASSERT_EQ(inst.argsSize_, DECLARED_ARG_SIZE);
    ASSERT_EQ(RecordArgAt(inst, MEM_INFO_OFFSET / sizeof(uintptr_t)), reinterpret_cast<uint64_t>(g_memInfo));
}

/**
 * |  用例集  | HijackedFuncOfAclrtLaunchKernelWithHostArgsTest
 * | 测试函数 | SanitizerPre
 * |  用例名  | sanitizer_pre_insert_record_arg_when_slot_covered_by_placeholder_data
 * | 用例描述 | 声明参数区末尾槽位被 placeholder 内嵌数据覆盖（argsSize=80、kernelArgSize=72、
 * |          | dataOffset=64）时，不能在 64 处覆盖数据，应按 kernel 读取偏移 64 插入 8 字节并后移数据。
 */
TEST_F(HijackedFuncOfAclrtLaunchKernelWithHostArgsTest,
    sanitizer_pre_insert_record_arg_when_slot_covered_by_placeholder_data)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    MockSanitizerPreCommon();
    MockStaticStubKernel();
    g_recordArgProbe.Reset();
    MOCKER(aclrtFunctionGetParamCountImplOrigin).stubs().will(invoke(GetParamCountFailStub));

    // 前 64 字节是用户参数，64 之后是 placeholder 指向的内嵌数据
    std::vector<uint8_t> hostArgs(80, 0);
    for (size_t i = MEM_INFO_OFFSET; i < hostArgs.size(); ++i) {
        hostArgs[i] = 0xAB;
    }
    aclrtPlaceHolderInfo placeHolderArray{0, MEM_INFO_OFFSET};
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl inst;
    aclrtLaunchKernelCfg cfg{};
    inst.Pre(funcHandle_, 3, &placeholder_, &cfg, hostArgs.data(), hostArgs.size(), &placeHolderArray, 1);

    EXPECT_FALSE(inst.skipSanitizer_);
    // 走的是“插入”而不是“覆盖”：参数区变长 8 字节，内嵌数据整体后移，未被记录区指针覆盖
    EXPECT_EQ(inst.argsSize_, hostArgs.size() + sizeof(uintptr_t));
    EXPECT_EQ(inst.placeHolderArray_.at(0).dataOffset, MEM_INFO_OFFSET + sizeof(uintptr_t));
    EXPECT_EQ(RecordArgAt(inst, MEM_INFO_OFFSET / sizeof(uintptr_t)), reinterpret_cast<uint64_t>(g_memInfo));
}

/**
 * |  用例集  | HijackedFuncOfAclrtLaunchKernelWithHostArgsTest
 * | 测试函数 | SanitizerPre
 * |  用例名  | sanitizer_pre_skip_when_record_arg_slot_occupied
 * | 用例描述 | 目标槽位已被占用（TRITON_DEVICE_PRINT=1 写入 DTData 指针）时，放弃本次检测：
 * |          | 不覆盖槽位、不上报内存、不初始化记录区，kernel 原样下发。
 */
TEST_F(HijackedFuncOfAclrtLaunchKernelWithHostArgsTest, sanitizer_pre_skip_when_record_arg_slot_occupied)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    MockSanitizerPreCommon();
    MockStaticStubKernel();
    g_recordArgProbe.Reset();
    MOCKER(aclrtFunctionGetParamCountImplOrigin).stubs().will(invoke(GetParamCountFailStub));

    std::vector<uint8_t> hostArgs(DECLARED_ARG_SIZE, 0);
    static_cast<uint64_t *>(static_cast<void *>(hostArgs.data()))[MEM_INFO_OFFSET / sizeof(uintptr_t)] = 0xDEADBEEFULL;
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl inst;
    aclrtLaunchKernelCfg cfg{};
    inst.Pre(funcHandle_, 3, &placeholder_, &cfg, hostArgs.data(), hostArgs.size(), nullptr, 0);

    ASSERT_TRUE(inst.skipSanitizer_);
    ASSERT_EQ(inst.memInfo_, nullptr);
    ASSERT_EQ(RecordArgAt(inst, MEM_INFO_OFFSET / sizeof(uintptr_t)), 0xDEADBEEFULL);
    ASSERT_EQ(inst.argsSize_, DECLARED_ARG_SIZE);
}

/**
 * |  用例集  | HijackedFuncOfAclrtLaunchKernelWithHostArgsTest
 * | 测试函数 | SanitizerPre
 * |  用例名  | sanitizer_pre_non_static_stub_keeps_append_behavior
 * | 用例描述 | 非静态插桩（动态插桩）kernel 不进入填槽分支，保持原“尾部追加”语义。
 */
TEST_F(HijackedFuncOfAclrtLaunchKernelWithHostArgsTest, sanitizer_pre_non_static_stub_keeps_append_behavior)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    MockSanitizerPreCommon();
    g_recordArgProbe.Reset();

    std::vector<uint8_t> hostArgs(DECLARED_ARG_SIZE, 0);
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl inst;
    aclrtLaunchKernelCfg cfg{};
    inst.Pre(funcHandle_, 3, &placeholder_, &cfg, hostArgs.data(), hostArgs.size(), nullptr, 0);

    ASSERT_EQ(g_recordArgProbe.paramCountCalls, 0U);
    ASSERT_EQ(inst.argsSize_, DECLARED_ARG_SIZE + sizeof(uintptr_t));
    ASSERT_EQ(RecordArgAt(inst, DECLARED_ARG_SIZE / sizeof(uintptr_t)), reinterpret_cast<uint64_t>(g_memInfo));
}

/**
 * |  用例集  | HijackedFuncOfAclrtLaunchKernelWithHostArgsTest
 * | 测试函数 | SanitizerPre
 * |  用例名  | sanitizer_pre_recycle_reported_mem_when_init_failed
 * | 用例描述 | __sanitizer_init 失败提前返回时，Pre 已上报的 section / input 内存必须在返回前成对回收，
 * |          | 否则 Post 因 memInfo_ 为空不会进入回收分支，造成 malloc/free 不配对。
 */
TEST_F(HijackedFuncOfAclrtLaunchKernelWithHostArgsTest, sanitizer_pre_recycle_reported_mem_when_init_failed)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    MockSanitizerPreCommon(false);
    MockStaticStubKernel();
    g_sectionsFreeCalls = 0;
    g_opFreeCalls = 0;

    std::vector<uint8_t> hostArgs(DECLARED_ARG_SIZE, 0);
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl inst;
    aclrtLaunchKernelCfg cfg{};
    inst.Pre(funcHandle_, 3, &placeholder_, &cfg, hostArgs.data(), hostArgs.size(), nullptr, 0);

    ASSERT_EQ(g_sectionsFreeCalls, 1U);
    ASSERT_EQ(g_opFreeCalls, 1U);
    ASSERT_EQ(inst.argsSize_, DECLARED_ARG_SIZE);
    // 静态插桩 kernel 没有未插桩版本可退，缺少 memInfo 时必须阻止本次下发
    ASSERT_TRUE(inst.abortLaunch_);
}

/**
 * |  用例集  | HijackedFuncOfAclrtLaunchKernelWithHostArgsTest
 * | 测试函数 | SanitizerPre
 * |  用例名  | sanitizer_pre_refresh_dbi_args_size_before_dbi_task
 * | 用例描述 | RunDBITask 在 ExpandArgs 之前执行，必须先把 DBITaskConfig::argsSize_ 刷新为本次 launch
 * |          | 的真实偏移，否则动态插桩会按上一次 launch 的残值编译（--tune-argsize 错误）。
 */
TEST_F(HijackedFuncOfAclrtLaunchKernelWithHostArgsTest, sanitizer_pre_refresh_dbi_args_size_before_dbi_task)
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    MockSanitizerPreCommon();
    DBITaskConfig::Instance().argsSize_ = 8; // 模拟上一次 launch 留下的残值
    g_dbiArgsSizeSeen = 0;

    std::vector<uint8_t> hostArgs(DECLARED_ARG_SIZE, 0);
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl inst;
    aclrtLaunchKernelCfg cfg{};
    inst.Pre(funcHandle_, 3, &placeholder_, &cfg, hostArgs.data(), hostArgs.size(), nullptr, 0);

    ASSERT_EQ(g_dbiArgsSizeSeen, DECLARED_ARG_SIZE);
    DBITaskConfig::Instance().argsSize_ = 0;
}
