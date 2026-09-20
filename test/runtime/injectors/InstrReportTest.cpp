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
#include <random>
#include <string>

#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/Communication.h"
#include "core/DomainSocket.h"
#include "core/HijackedLayerManager.h"
#include "core/LocalProcess.h"
#include "acl.h"
#include "mockcpp/mockcpp.hpp"
#define private public
#include "runtime/inject_helpers/DevMemManager.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/ConfigManager.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#undef private
#include "runtime/inject_helpers/InstrReport.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/InteractHelper.h"

#include "helper/MockHelper.h"

using namespace std;

namespace {

class InstrReportTest : public testing::Test {
public:
    void SetUp() override
    {
        server_.reset(new Server(CommType::SOCKET));
        MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    }
    void TearDown() override
    {
        GlobalMockObject::verify();
    }

    std::shared_ptr<Server> server_;
};

uint64_t RandUint(uint64_t min, uint64_t max)
{
    static std::mt19937_64 mersenneEngine(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist(min, max);
    return dist(mersenneEngine);
}

aclError MemoryMallocStub(DevMemManager *, void **memPtr, uint64_t memSize)
{
    static int stubObject;
    *memPtr = &stubObject;
    return ACL_ERROR_NONE;
}

static KernelContext::RegisterEvent g_registerEvent;

bool GetRegisterEventStub(KernelContext *, uint64_t regId, KernelContext::RegisterEvent &event)
{
    event = g_registerEvent;
    return true;
}

aclError MemcpyNoRecordStub(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind)
{
    RecordBlockHead recordBlockHead {};
    *static_cast<RecordBlockHead*>(dst) = recordBlockHead;
    return ACL_ERROR_NONE;
}

aclError MemcpyStub(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind)
{
    copy_n(static_cast<const uint8_t*>(src), count, static_cast<uint8_t*>(dst));
    return RT_ERROR_NONE;
}

aclError MallocOriginStub(void **devPtr, size_t size, aclrtMemMallocPolicy policy)
{
    if (devPtr == nullptr) {
        return ACL_ERROR_BAD_ALLOC;
    }
    *devPtr = malloc(size);
    return ACL_ERROR_NONE;
}

aclError MallocMemoryStub(DevMemManager *, void **memPtr, uint64_t size)
{
    *memPtr = malloc(size);
    return ACL_ERROR_NONE;
}
aclError MemcpySomeRecordStub(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind)
{
    RecordBlockHead recordBlockHead {};
    constexpr uint64_t blockHeadOffset = 123;
    constexpr uint64_t blockHeadWriteOffset = 123;
    constexpr uint64_t blockHeadRecordCount = 10;
    constexpr uint64_t blockHeadRecordWriteCount = 10;
    constexpr uint64_t totalBlockDim = 5;
    recordBlockHead.offset = blockHeadOffset;
    recordBlockHead.writeOffset = blockHeadWriteOffset;
    recordBlockHead.recordCount = blockHeadRecordCount;
    recordBlockHead.recordWriteCount = blockHeadRecordWriteCount;
    *static_cast<RecordBlockHead*>(dst) = recordBlockHead;
    return ACL_ERROR_NONE;
}

aclError GetDeviceOriginStub(int32_t *devId)
{
    static int curDevId = 0;
    *devId = ++curDevId;
    return ACL_ERROR_NONE;
}

}  // namespace [Dummy]

TEST_F(InstrReportTest, init_with_invalid_block_dim_expect_return_nullptr)
{
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(SanitizerConfig()));
    ASSERT_EQ(__sanitizer_init(0), nullptr);
    ASSERT_EQ(__sanitizer_init(MAX_BLOCKDIM_NUMS + 1), nullptr);
}

TEST_F(InstrReportTest, init_with_rtMalloc_failed_expect_return_nullptr)
{
    MOCKER(&DevMemManager::MallocMemory).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(SanitizerConfig ()));

    ASSERT_EQ(__sanitizer_init(MAX_BLOCKDIM_NUMS), nullptr);
    GlobalMockObject::verify();
}

TEST_F(InstrReportTest, init_with_rtMemcpy_failed_expect_return_nullptr)
{
    MOCKER(&DevMemManager::MallocMemory).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(SanitizerConfig ()));

    ASSERT_EQ(__sanitizer_init(MAX_BLOCKDIM_NUMS), nullptr);
    GlobalMockObject::verify();
}

TEST_F(InstrReportTest, init_with_initialized_memory_expect_return_resued_memory)
{
    MOCKER(&DevMemManager::MallocMemory).stubs().will(invoke(&MemoryMallocStub));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&rtGetL2CacheOffsetOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    DevMemManager::Instance().SetMemoryInitFlag(true);
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(SanitizerConfig ()));

    ASSERT_NE(__sanitizer_init(MAX_BLOCKDIM_NUMS), nullptr);
    GlobalMockObject::verify();
}

TEST_F(InstrReportTest, init_with_rtMemset_success_expect_return_valid_memory)
{
    MOCKER(&DevMemManager::MallocMemory).stubs().will(invoke(&MemoryMallocStub));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&aclrtMemsetImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&rtGetL2CacheOffsetOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    DevMemManager::Instance().SetMemoryInitFlag(false);
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(SanitizerConfig ()));

    ASSERT_NE(__sanitizer_init(MAX_BLOCKDIM_NUMS), nullptr);
    ASSERT_TRUE(DevMemManager::Instance().IsMemoryInit());
    GlobalMockObject::verify();
}

TEST_F(InstrReportTest, init_with_different_kernel_type_expect_return_correct_memory_size)
{
    MOCKER(&KernelContext::GetRegisterEvent).stubs().will(invoke(&GetRegisterEventStub));
    MOCKER(&DevMemManager::MallocMemory).stubs().will(invoke(&MemoryMallocStub));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&aclrtMemsetImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&rtGetL2CacheOffsetOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    DevMemManager::Instance().SetMemoryInitFlag(false);
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(SanitizerConfig ()));

    g_registerEvent.bin.magic = RT_DEV_BINARY_MAGIC_ELF_AICPU;
    ASSERT_NE(__sanitizer_init(MAX_BLOCKDIM_NUMS), nullptr);
    g_registerEvent.bin.magic = RT_DEV_BINARY_MAGIC_ELF;
    ASSERT_NE(__sanitizer_init(MAX_BLOCKDIM_NUMS), nullptr);
    g_registerEvent.bin.magic = RT_DEV_BINARY_MAGIC_ELF_AIVEC;
    ASSERT_NE(__sanitizer_init(MAX_BLOCKDIM_NUMS), nullptr);
    g_registerEvent.bin.magic = RT_DEV_BINARY_MAGIC_ELF_AICUBE;
    ASSERT_NE(__sanitizer_init(MAX_BLOCKDIM_NUMS), nullptr);
    GlobalMockObject::verify();
}

TEST_F(InstrReportTest, finalize_with_uninitalized_memory_expect_return)
{
    DevMemManager::Instance().SetMemoryInitFlag(false);
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(SanitizerConfig ()));

    __sanitizer_finalize(nullptr, MAX_BLOCKDIM_NUMS);
}

TEST_F(InstrReportTest, finalize_with_invalid_block_dim_expect_return)
{
    DevMemManager::Instance().SetMemoryInitFlag(true);
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(SanitizerConfig ()));

    __sanitizer_finalize(nullptr, MAX_BLOCKDIM_NUMS + 1);
}

TEST_F(InstrReportTest, finalize_with_rtMemcpy_block_head_failed_expect_return)
{
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    DevMemManager::Instance().SetMemoryInitFlag(true);
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(SanitizerConfig ()));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));

    __sanitizer_finalize(nullptr, MAX_BLOCKDIM_NUMS);
    GlobalMockObject::verify();
}

TEST_F(InstrReportTest, finalize_with_blocks_of_no_record_expect_return)
{
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(invoke(MemcpyNoRecordStub));
    DevMemManager::Instance().SetMemoryInitFlag(true);
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(SanitizerConfig ()));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));

    __sanitizer_finalize(nullptr, 1);
    GlobalMockObject::verify();
}

TEST_F(InstrReportTest, finalize_with_blocks_of_records_expect_return)
{
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(invoke(MemcpySomeRecordStub));
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));
    std::string msg {"response"};
    MockHelper::Instance().SetMsg(msg);
    MOCKER(&LocalDevice::Wait)
        .stubs()
        .will(invoke(MockHelper::WaitMockFuncF));
    DevMemManager::Instance().SetMemoryInitFlag(true);
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(SanitizerConfig ()));

    __sanitizer_finalize(nullptr, 1);
    GlobalMockObject::verify();
}

TEST_F(InstrReportTest, input_valid_soc_info_expect_correct_arch_name)
{
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(SanitizerConfig()));
    vector<tuple<KernelType, string, string>> testCases{
        {KernelType::AIVEC, "Ascend910B", "dav-c220-vec"},
        {KernelType::AICUBE, "Ascend910B", "dav-c220-cube"},
        {KernelType::MIX, "Ascend910B", "dav-c220"},
        {KernelType::AIVEC, "Ascend310P", "dav-m200-vec"},
        {KernelType::MIX, "Ascend310P", "dav-m200"},
    };
    for (const auto &[kernelType, soc, expect]: testCases) {
        string answer = GetArchName(kernelType, soc);
        EXPECT_EQ(answer, expect);
    }
}

TEST_F(InstrReportTest, input_invalid_soc_info_expect_correct_arch_name)
{
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(SanitizerConfig()));
    vector<tuple<KernelType, string, string>> testCases{
        {KernelType::AIVEC, "Invalid", ""},
        {KernelType::INVALID, "Ascend910B", ""},
        {KernelType::MIX, "Ascend910", ""},
        {KernelType::AICUBE, "Ascend310P", ""},
        {KernelType::MIX, "Ascend310", ""},
    };
    for (const auto &[kernelType, soc, expect]: testCases) {
        string answer = GetArchName(kernelType, soc);
        EXPECT_EQ(answer, expect);
    }
}


TEST_F(InstrReportTest, init_with_invalid_zero_cache_size_expect_return_nullptr)
{
    MOCKER(&KernelContext::GetRegisterEvent).stubs().will(invoke(&GetRegisterEventStub));
    MOCKER(&DevMemManager::MallocMemory).stubs().will(invoke(&MemoryMallocStub));
    MOCKER(&aclrtGetDeviceImplOrigin).stubs().will(invoke(&GetDeviceOriginStub));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&aclrtMemsetImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    DevMemManager::Instance().SetMemoryInitFlag(false);
    SanitizerConfig config{};
    config.cacheSize = 0;
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(config));

    DevMemManager::Instance().SetMemoryInitFlag(false);
    ASSERT_EQ(__sanitizer_init(1), nullptr);
    GlobalMockObject::verify();
}

TEST_F(InstrReportTest, init_with_ascend310p_single_check_and_invalid_max_cache_size_expect_return_nullptr)
{
    MOCKER(&KernelContext::GetRegisterEvent).stubs().will(invoke(&GetRegisterEventStub));
    MOCKER(&DevMemManager::MallocMemory).stubs().will(invoke(&MemoryMallocStub));
    MOCKER(&aclrtGetDeviceImplOrigin).stubs().will(invoke(&GetDeviceOriginStub));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&aclrtMemsetImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&DeviceContext::GetSocVersion).stubs().will(returnValue(string("Ascend310P3")));
    DevMemManager::Instance().SetMemoryInitFlag(false);
    SanitizerConfig config{};
    config.checkBlockId = 0;
    config.cacheSize = MAX_RECORD_BUF_SIZE_EACH_BLOCK + 1;
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(config));

    DevMemManager::Instance().SetMemoryInitFlag(false);
    ASSERT_EQ(__sanitizer_init(1), nullptr);
    GlobalMockObject::verify();
}

TEST_F(InstrReportTest, init_with_ascend910b_single_check_and_invalid_max_cache_size_expect_return_nullptr)
{
    MOCKER(&KernelContext::GetRegisterEvent).stubs().will(invoke(&GetRegisterEventStub));
    MOCKER(&DevMemManager::MallocMemory).stubs().will(invoke(&MemoryMallocStub));
    MOCKER(&aclrtGetDeviceImplOrigin).stubs().will(invoke(&GetDeviceOriginStub));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&aclrtMemsetImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&DeviceContext::GetSocVersion).stubs().will(returnValue(string("Ascend910B1")));
    DevMemManager::Instance().SetMemoryInitFlag(false);
    constexpr int16_t chkBlkIdx = 5;
    SanitizerConfig config{};
    config.checkBlockId = chkBlkIdx;
    config.cacheSize = MAX_RECORD_BUF_SIZE_EACH_BLOCK + 1;

    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(config));
    DevMemManager::Instance().SetMemoryInitFlag(false);
    ASSERT_EQ(__sanitizer_init(1), nullptr);
    GlobalMockObject::verify();
}

TEST_F(InstrReportTest, init_with_valid_cache_size_expect_return_success)
{
    MOCKER(&KernelContext::GetRegisterEvent).stubs().will(invoke(&GetRegisterEventStub));
    MOCKER(&DevMemManager::MallocMemory).stubs().will(invoke(&MemoryMallocStub));
    MOCKER(&aclrtGetDeviceImplOrigin).stubs().will(invoke(&GetDeviceOriginStub));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&aclrtMemsetImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&rtGetL2CacheOffsetOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&DeviceContext::GetSocVersion).stubs().will(returnValue(string("Ascend310P3")));
    DevMemManager::Instance().SetMemoryInitFlag(false);

    SanitizerConfig config{};
    constexpr uint64_t upLimBlkId = 100;
    config.checkBlockId = RandUint(0, upLimBlkId);
    config.cacheSize = RandUint(1, MAX_RECORD_BUF_SIZE_EACH_BLOCK);

    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(config));
    DevMemManager::Instance().SetMemoryInitFlag(false);
    ASSERT_NE(__sanitizer_init(1), nullptr);
    GlobalMockObject::verify();
}

/**
 * | 用例集  | InstrReportTest
 * | 测试函数 | __sanitizer_finalize → ReportParaBase
 * | 用例名  | finalize_with_simt_launch_stack_expect_register_implicit_param
 * | 用例描述 | 当处于 SIMT launch 调用栈（LayerGuard 含 aclrtLaunchSIMTKernelWithHostArgsImpl）时，
 * |          | paraBase.addr 有效，ReportParaBase 应额外注册 32B 隐式参数区：
 * |          | ReportMalloc 调 2 次（隐式参数区 + paraBase 本体），ReportMemset 同，
 * |          | ReportFree 调 2 次（隐式参数区 + paraBase 本体）。
 */
/**
 * | 用例集  | InstrReportTest
 * | 测试函数 | __sanitizer_finalize → ReportParaBase (IsSimtLaunchCallStack)
 * | 用例名  | simt_launch_stack_in_call_stack_expect_true
 * | 用例描述 | 当处于 SIMT launch 调用栈（LayerGuard 含 aclrtLaunchSIMTKernelWithHostArgsImpl）时，
 * |          | HijackedLayerManager::InCallStack 应返回 true，
 * |          | 从而使 ReportParaBase 走 32B 隐式参数补偿分支。
 * |          | 注：ReportParaBase 在 LaunchInitFinalize.cpp 匿名 namespace 内，跨编译单元 mock ReportMalloc
 * |          | 不可行，故此处验证 IsSimtLaunchCallStack 的判定依据——HijackedLayerManager 栈状态。
 */
TEST_F(InstrReportTest, simt_launch_stack_in_call_stack_expect_true)
{
    auto &layerMgr = HijackedLayerManager::Instance();
    layerMgr.Layers().clear();
    layerMgr.Push("aclrtLaunchSIMTKernelWithHostArgsImpl");
    ASSERT_TRUE(layerMgr.InCallStack("aclrtLaunchSIMTKernelWithHostArgsImpl"));
    ASSERT_TRUE(layerMgr.InCallStack("aclrtLaunchSIMTKernelWithArgsArrayImpl") == false);
    layerMgr.Pop();
    ASSERT_FALSE(layerMgr.InCallStack("aclrtLaunchSIMTKernelWithHostArgsImpl"));
}

/**
 * | 用例集  | InstrReportTest
 * | 测试函数 | __sanitizer_finalize → ReportParaBase (IsSimtLaunchCallStack)
 * | 用例名  | non_simt_launch_stack_in_call_stack_expect_false
 * | 用例描述 | 当处于非 SIMT launch 调用栈（LayerGuard 含 aclrtLaunchKernelImpl，C310 设备）时，
 * |          | HijackedLayerManager::InCallStack 对两个 SIMT launch 接口名均返回 false，
 * |          | 从而使 ReportParaBase 不走 32B 隐式参数补偿分支。
 * |          | 回归 isSimtLaunch 与 supportSimt 解耦：C310 设备但走普通 launch，不应触发 32B 补偿。
 */
TEST_F(InstrReportTest, non_simt_launch_stack_in_call_stack_expect_false)
{
    auto &layerMgr = HijackedLayerManager::Instance();
    layerMgr.Layers().clear();
    layerMgr.Push("aclrtLaunchKernelImpl");
    ASSERT_FALSE(layerMgr.InCallStack("aclrtLaunchSIMTKernelWithHostArgsImpl"));
    ASSERT_FALSE(layerMgr.InCallStack("aclrtLaunchSIMTKernelWithArgsArrayImpl"));
    ASSERT_TRUE(layerMgr.InCallStack("aclrtLaunchKernelImpl"));
    layerMgr.Pop();
}
