/* -------------------------------------------------------------------------
 *  This file is part of the MindStudio project.
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

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <gtest/gtest.h>

#include "acl_rt_impl/AscendclImplOrigin.h"
#include "ascend_hal/AscendHalOrigin.h"
#include "mockcpp/mockcpp.hpp"
#include "profapi/ProfOriginal.h"
#include "runtime/inject_helpers/DFXKernelLauncher.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/L2CacheCleaner.h"
#include "runtime/inject_helpers/MemoryContext.h"
#include "runtime/inject_helpers/MsTx.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "runtime/inject_helpers/ThreadContext.h"
#include "utils/FileSystem.h"
#include "utils/InjectLogger.h"

namespace {

struct ClearL2Observation {
    std::vector<size_t> allocations;
    uint8_t buffers[16][32]{};
    std::vector<void *> freedBuffers;
    std::vector<void *> freeAttempts;
    void *failFreeBuffer = nullptr;
    uint32_t freeFailuresRemaining = 0;
    size_t allocationAttempts = 0;
    size_t failAllocationOn = 0;
    uint64_t hostTiling = 0;
    uint64_t copiedTiling = 0;
    uint32_t blockDim = 0;
    uint32_t clearCalls = 0;
    uint32_t freeCalls = 0;
    uint32_t hostFreeCalls = 0;
    uint32_t syncCalls = 0;
    uint32_t failSyncOn = 0;
    uint32_t prefetchCalls = 0;
    uint32_t cleanerResetCalls = 0;
    std::vector<unsigned int> profileChannels;
    bool failHostAllocation = false;
    bool failHostFree = false;
    bool failHostToDeviceCopy = false;
    bool failDeviceCopy = false;
    bool failPrefetch = false;
    bool failClearKernel = false;
    bool appReplay = false;
    bool restoreSucceeds = false;
    aclrtStream expectedStream = nullptr;
    int64_t l2Bytes = 100663296;
    int64_t cores = 20;
    aclrtDevAttr failingAttribute = ACL_DEV_ATTR_L2_CACHE_SIZE;
    bool failQuery = false;
    std::vector<aclrtDevAttr> attributes;
};

ClearL2Observation *observation = nullptr;

bool IsApplicationReplay() {
    return observation->appReplay;
}

bool RestoreReplayMemory() {
    return observation->restoreSucceeds;
}

void ObserveCleanerReset(L2CacheCleaner *) {
    ++observation->cleanerResetCalls;
}

int ObserveProfileStart(unsigned int deviceId, unsigned int channelId, struct prof_start_para *) {
    EXPECT_EQ(7U, deviceId);
    observation->profileChannels.push_back(channelId);
    return 0;
}

aclError ObserveModelExecution(aclmdlRI, aclrtStream stream) {
    EXPECT_EQ(observation->expectedStream, stream);
    EXPECT_EQ(0U, observation->freeCalls);
    return ACL_SUCCESS;
}

aclError QueryTargetAttribute(uint32_t device, aclrtDevAttr attr, int64_t *value) {
    EXPECT_EQ(7U, device);
    observation->attributes.push_back(attr);
    if (observation->failQuery && attr == observation->failingAttribute) {
        return ACL_ERROR_INTERNAL_ERROR;
    }
    if (attr == ACL_DEV_ATTR_L2_CACHE_SIZE) {
        *value = observation->l2Bytes;
    } else {
        EXPECT_EQ(ACL_DEV_ATTR_AICORE_CORE_NUM, attr);
        *value = observation->cores;
    }
    return ACL_SUCCESS;
}

aclError AllocateDeviceBuffer(void **ptr, size_t size, aclrtMemMallocPolicy) {
    if (++observation->allocationAttempts == observation->failAllocationOn) {
        return ACL_ERROR_BAD_ALLOC;
    }
    const size_t index = observation->allocations.size();
    if (index >= 16) {
        ADD_FAILURE() << "Unexpected device allocation";
        return ACL_ERROR_INTERNAL_ERROR;
    }
    observation->allocations.push_back(size);
    // 模拟设备地址，不实际分配数百 MiB；设备拷贝和 kernel 调用均由测试观察。
    *ptr = observation->buffers[index];
    return ACL_SUCCESS;
}

aclError CopyTiling(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind, aclrtStream stream) {
    EXPECT_EQ(observation->expectedStream, stream);
    if (observation->allocations.size() < 5) {
        ADD_FAILURE() << "Unexpected copy before all L2CacheCleaner buffers are allocated";
        return ACL_ERROR_INTERNAL_ERROR;
    }
    const size_t offset = observation->allocations.size() - 5;
    if (kind == ACL_MEMCPY_HOST_TO_DEVICE) {
        EXPECT_EQ(static_cast<void *>(observation->buffers[offset + 4]), dst);
        EXPECT_EQ(sizeof(uint64_t), destMax);
        EXPECT_EQ(static_cast<const void *>(&observation->hostTiling), src);
        EXPECT_EQ(sizeof(uint64_t), count);
        if (observation->failHostToDeviceCopy) {
            return ACL_ERROR_INTERNAL_ERROR;
        }
        if (src != &observation->hostTiling) {
            return ACL_ERROR_INTERNAL_ERROR;
        }
        std::memcpy(&observation->copiedTiling, src, sizeof(uint64_t));
    } else if (kind == ACL_MEMCPY_DEVICE_TO_DEVICE) {
        const size_t bytes = observation->allocations[offset];
        EXPECT_EQ(static_cast<void *>(observation->buffers[offset + 1]), dst);
        EXPECT_EQ(bytes, destMax);
        EXPECT_EQ(static_cast<const void *>(observation->buffers[offset]), src);
        EXPECT_EQ(bytes, count);
        if (observation->failDeviceCopy) {
            return ACL_ERROR_INTERNAL_ERROR;
        }
    } else {
        ADD_FAILURE() << "Unexpected copy kind: " << static_cast<int>(kind);
        return ACL_ERROR_INTERNAL_ERROR;
    }
    return ACL_SUCCESS;
}

aclError AllocateHostTiling(void **ptr, size_t size) {
    EXPECT_EQ(sizeof(uint64_t), size);
    if (observation->failHostAllocation) {
        return ACL_ERROR_BAD_ALLOC;
    }
    *ptr = &observation->hostTiling;
    return ACL_SUCCESS;
}

bool ObserveClearKernel(DFXKernelLauncher *, uint32_t blockDim, void *stream, std::vector<void *> &args) {
    EXPECT_EQ(observation->expectedStream, stream);
    EXPECT_EQ(2U, args.size());
    if (args.size() == 2) {
        const size_t offset = observation->allocations.size() - 5;
        EXPECT_EQ(static_cast<void *>(observation->buffers[offset + 3]), args[0]);
        EXPECT_EQ(static_cast<void *>(observation->buffers[offset + 4]), args[1]);
    }
    observation->blockDim = blockDim;
    ++observation->clearCalls;
    return !observation->failClearKernel;
}

aclError ObserveFree(void *ptr) {
    EXPECT_EQ(0, std::count(observation->freedBuffers.begin(), observation->freedBuffers.end(), ptr));
    observation->freeAttempts.push_back(ptr);
    if (ptr == observation->failFreeBuffer && observation->freeFailuresRemaining != 0) {
        --observation->freeFailuresRemaining;
        return ACL_ERROR_INTERNAL_ERROR;
    }
    observation->freedBuffers.push_back(ptr);
    ++observation->freeCalls;
    return ACL_SUCCESS;
}

aclError ObserveHostFree(void *ptr) {
    EXPECT_EQ(static_cast<void *>(&observation->hostTiling), ptr);
    ++observation->hostFreeCalls;
    return observation->failHostFree ? ACL_ERROR_INTERNAL_ERROR : ACL_SUCCESS;
}

aclError ObserveSynchronize(aclrtStream stream) {
    EXPECT_EQ(observation->expectedStream, stream);
    if (++observation->syncCalls == observation->failSyncOn) {
        return ACL_ERROR_INTERNAL_ERROR;
    }
    return ACL_SUCCESS;
}

aclError ObservePrefetch(void *ptr, size_t size, aclrtCmoType type, aclrtStream stream) {
    if (observation->allocations.size() < 5) {
        ADD_FAILURE() << "Unexpected prefetch before all L2CacheCleaner buffers are allocated";
        return ACL_ERROR_INTERNAL_ERROR;
    }
    const size_t offset = observation->allocations.size() - 5;
    EXPECT_EQ(static_cast<void *>(observation->buffers[offset + 2]), ptr);
    EXPECT_EQ(observation->allocations[offset], size);
    EXPECT_EQ(ACL_RT_CMO_TYPE_PREFETCH, type);
    EXPECT_EQ(observation->expectedStream, stream);
    ++observation->prefetchCalls;
    return observation->failPrefetch ? ACL_ERROR_INTERNAL_ERROR : ACL_SUCCESS;
}

class ClearL2ParamTest : public testing::Test {
protected:
    void SetUp() override {
        previousConfig_ = ProfConfig::Instance().GetConfig();
        previousSoc_ = DeviceContext::Instance(0).GetSocVersion();
        char path[] = "./clear_l2_param_XXXXXX";
        ASSERT_NE(nullptr, mkdtemp(path));
        path_ = path;
        observation = &observed_;

        MessageOfProfConfig config{};
        config.profWarmUpTimes = 0;
        ProfConfig::Instance().Init(config);
        MOCKER(&ProfConfig::IsAppReplay).stubs().will(invoke(IsApplicationReplay));
        MOCKER(&ProfConfig::IsRangeReplay).stubs().will(returnValue(false));
        MOCKER(&ProfConfig::IsSimulatorLaunchedByDevice).stubs().will(returnValue(false));
        MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path_));
        MOCKER(&MsTx::IsInMstxRange).stubs().will(returnValue(false));
        // 工具线程索引为0，采集对象保存设备7，属性查询必须使用后者。
        MOCKER(&ThreadContext::GetDeviceId).stubs().will(returnValue(0));
        MOCKER(DeviceContext::GetRunningDeviceId).stubs().will(returnValue(int32_t{7}));
        MOCKER(&aclrtGetDeviceImplOrigin).expects(never());
        MOCKER(&KernelContext::GetMC2Flag).stubs().will(returnValue(false));
        MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(false));
        MOCKER(&MemoryContext::Backup).stubs().will(returnValue(true));
        // 默认在输入恢复处停止；完整重放用例会显式打开恢复及 PMU 驱动桩。
        MOCKER(&MemoryContext::Restore).stubs().will(invoke(RestoreReplayMemory));
        MOCKER(profSetProfCommandOrigin).stubs().will(returnValue(ACL_SUCCESS));
        MOCKER(aclrtSynchronizeStreamImplOrigin).stubs().will(invoke(ObserveSynchronize));
        MOCKER(aclrtCmoAsyncImplOrigin).stubs().will(invoke(ObservePrefetch));
        MOCKER(aclrtMallocImplOrigin).stubs().will(invoke(AllocateDeviceBuffer));
        MOCKER(aclrtMallocHostImplOrigin).stubs().will(invoke(AllocateHostTiling));
        MOCKER(aclrtFreeHostImplOrigin).stubs().will(invoke(ObserveHostFree));
        MOCKER(aclrtMemcpyAsyncImplOrigin).stubs().will(invoke(CopyTiling));
        MOCKER(&DFXKernelLauncher::CallClearL2Cache).stubs().will(invoke(ObserveClearKernel));
        MOCKER(aclrtFreeImplOrigin).stubs().will(invoke(ObserveFree));
    }

    void TearDown() override {
        GlobalMockObject::verify();
        DeviceContext::Instance(0).SetSocVersion(previousSoc_);
        ProfConfig::Instance().Init(previousConfig_);
        if (!path_.empty()) {
            RemoveAll(path_);
        }
        observation = nullptr;
    }

    void Replay(const char *soc) {
        DeviceContext::Instance(0).SetSocVersion(soc);
        ProfDataCollect collector;
        EXPECT_FALSE(collector.ProfData(nullptr, [] {
            ADD_FAILURE() << "Profiling should stop before launching the application kernel";
            return true;
        }));
        // 采集对象仍存活，不能依赖成员析构才释放重放缓冲区。
        ExpectAllBuffersFreedOnce();
    }

    void EnableTwoReplayRounds() {
        observed_.restoreSucceeds = true;
        // 成功采集会保存基本信息，提供 blockDim，避免额外查询真实设备上下文。
        MOCKER(&KernelContext::GetBlockId).stubs().will(returnValue(uint32_t{1}));
        MessageOfProfConfig config{};
        config.profWarmUpTimes = 0;
        config.aicPmu[0] = 1;
        config.aicPmu[PMU_EVENT_MAX_NUM] = 2;
        ProfConfig::Instance().Init(config);
        MOCKER(prof_drv_start_origin).stubs().will(invoke(ObserveProfileStart));
        MOCKER(prof_stop_origin).stubs().will(returnValue(0));
        MOCKER(prof_channel_poll_origin).stubs().will(returnValue(0));
        MOCKER(aclrtSynchronizeStreamWithTimeoutImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    }

    void ConfigureRangeReplay() {
        RangeReplayConfig config{true, 0, nullptr, {"kernel-output"}};
        MOCKER(&ProfDataCollect::GetThreadRangeConfigMap).stubs().will(returnValue(config));
        MOCKER(GetEnv).stubs().will(returnValue(path_));
    }

    void ExpectNoCleanerUse() {
        MOCKER(&L2CacheCleaner::Prepare).expects(never());
        MOCKER(&L2CacheCleaner::Clear).expects(never());
        MOCKER(&L2CacheCleaner::Reset).stubs().will(invoke(ObserveCleanerReset));
    }

    void ExpectPrepared(uint64_t bytesPerCore, uint32_t cores) {
        const size_t bytes = bytesPerCore * cores;
        EXPECT_EQ((std::vector<size_t>{bytes, bytes, bytes, bytes, 32}), observed_.allocations);
        EXPECT_EQ(bytesPerCore, observed_.copiedTiling);
        EXPECT_EQ(cores, observed_.blockDim);
        EXPECT_EQ(1U, observed_.clearCalls);
        EXPECT_EQ(5U, observed_.freeCalls);
    }

    void ExpectAllBuffersFreedOnce() {
        EXPECT_EQ(observed_.allocations.size(), observed_.freedBuffers.size());
        for (size_t i = 0; i < observed_.allocations.size(); ++i) {
            EXPECT_EQ(1, std::count(observed_.freedBuffers.begin(), observed_.freedBuffers.end(),
                static_cast<void *>(observed_.buffers[i])));
        }
    }

    ClearL2Observation observed_;
    MessageOfProfConfig previousConfig_{};
    std::string previousSoc_;
    std::string path_;
};

TEST_F(ClearL2ParamTest, known_soc_uses_calibrated_tiling_without_attribute_queries) {
    MOCKER(&aclrtGetDeviceInfoImplOrigin).expects(never());
    Replay("Ascend910B4");
    ExpectPrepared(5242880, 20);
}

TEST_F(ClearL2ParamTest, known_950_tiling_is_tile_aligned_and_matches_buffer_sizes) {
    MOCKER(&aclrtGetDeviceInfoImplOrigin).expects(never());
    for (const char *soc : {"Ascend950DT_95A1", "Ascend950DT_95A2"}) {
        SCOPED_TRACE(soc);
        observed_ = ClearL2Observation{};
        Replay(soc);
        ExpectPrepared(3728384, 36);
        EXPECT_EQ(0U, observed_.copiedTiling % 512);
    }
}

TEST_F(ClearL2ParamTest, unknown_soc_queries_target_device_and_aligns_tiling) {
    MOCKER(&aclrtGetDeviceInfoImplOrigin).expects(exactly(2)).will(invoke(QueryTargetAttribute));
    Replay("Ascend910B9999");
    ExpectPrepared(5033472, 20);
    EXPECT_EQ(
        (std::vector<aclrtDevAttr>{ACL_DEV_ATTR_L2_CACHE_SIZE, ACL_DEV_ATTR_AICORE_CORE_NUM}), observed_.attributes);
}

TEST_F(ClearL2ParamTest, aligned_dynamic_tiling_does_not_add_an_extra_tile) {
    observed_.l2Bytes = 10240;
    MOCKER(&aclrtGetDeviceInfoImplOrigin).expects(exactly(2)).will(invoke(QueryTargetAttribute));
    Replay("Ascend910_93xx");
    ExpectPrepared(512, 20);
    EXPECT_EQ(1U, observed_.prefetchCalls);
}

TEST_F(ClearL2ParamTest, l2_query_failure_stops_before_core_query_or_buffer_preparation) {
    observed_.failQuery = true;
    MOCKER(&aclrtGetDeviceInfoImplOrigin).expects(once()).will(invoke(QueryTargetAttribute));
    Replay("Ascend910B9999");
    EXPECT_TRUE(observed_.allocations.empty());
    EXPECT_EQ(0U, observed_.copiedTiling);
    EXPECT_EQ(0U, observed_.clearCalls);
    EXPECT_EQ((std::vector<aclrtDevAttr>{ACL_DEV_ATTR_L2_CACHE_SIZE}), observed_.attributes);
}

TEST_F(ClearL2ParamTest, core_query_failure_stops_before_buffer_preparation) {
    observed_.failQuery = true;
    observed_.failingAttribute = ACL_DEV_ATTR_AICORE_CORE_NUM;
    MOCKER(&aclrtGetDeviceInfoImplOrigin).expects(exactly(2)).will(invoke(QueryTargetAttribute));
    Replay("Ascend950PR_95A9");
    EXPECT_TRUE(observed_.allocations.empty());
    EXPECT_EQ(0U, observed_.copiedTiling);
    EXPECT_EQ(0U, observed_.clearCalls);
    EXPECT_EQ(
        (std::vector<aclrtDevAttr>{ACL_DEV_ATTR_L2_CACHE_SIZE, ACL_DEV_ATTR_AICORE_CORE_NUM}), observed_.attributes);
}

TEST_F(ClearL2ParamTest, application_replay_does_not_prepare_cleaner) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    observed_.appReplay = true;
    ExpectNoCleanerUse();
    MOCKER(prof_drv_start_origin).stubs().will(returnValue(-1));
    MOCKER(prof_stop_origin).stubs().will(returnValue(0));
    {
        ProfDataCollect collector;
        EXPECT_FALSE(collector.ProfData(nullptr, [] {
            ADD_FAILURE() << "Profiling should stop before launching the application kernel";
            return true;
        }));
    }
    EXPECT_TRUE(observed_.allocations.empty());
    EXPECT_EQ(0U, observed_.clearCalls);
    EXPECT_EQ(0U, observed_.freeCalls);
    EXPECT_EQ(0U, observed_.cleanerResetCalls);
}

TEST_F(ClearL2ParamTest, successful_application_replay_does_not_create_or_reset_cleaner) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    EnableTwoReplayRounds();
    observed_.appReplay = true;
    ExpectNoCleanerUse();
    MOCKER(&DFXKernelLauncher::CallEmptyKernel).expects(never());
    uint32_t launches = 0;
    {
        ProfDataCollect collector;
        EXPECT_TRUE(collector.ProfData(nullptr, [&] {
            ++launches;
            return true;
        }));
    }
    EXPECT_EQ(1U, launches);
    EXPECT_TRUE(observed_.allocations.empty());
    EXPECT_EQ(0U, observed_.clearCalls);
    EXPECT_EQ(0U, observed_.freeCalls);
    EXPECT_EQ(0U, observed_.cleanerResetCalls);
}

TEST_F(ClearL2ParamTest, application_replay_preserves_l2_metrics_without_creating_cleaner) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    EnableTwoReplayRounds();
    observed_.appReplay = true;
    MessageOfProfConfig config = ProfConfig::Instance().GetConfig();
    config.aivPmu[0] = 1;
    config.l2CachePmu[0] = 1;
    ProfConfig::Instance().Init(config);
    ExpectNoCleanerUse();
    MOCKER(&DFXKernelLauncher::CallEmptyKernel).expects(once()).will(returnValue(true));
    uint32_t launches = 0;
    {
        ProfDataCollect collector;
        EXPECT_TRUE(collector.ProfData(nullptr, [&] {
            ++launches;
            return true;
        }));
    }
    EXPECT_EQ(1U, launches);
    EXPECT_EQ((std::vector<unsigned int>{CHANNEL_TSFW_L2}), observed_.profileChannels);
    EXPECT_TRUE(observed_.allocations.empty());
    EXPECT_EQ(0U, observed_.clearCalls);
    EXPECT_EQ(0U, observed_.freeCalls);
    EXPECT_EQ(0U, observed_.cleanerResetCalls);
}

TEST_F(ClearL2ParamTest, application_replay_without_rounds_does_not_create_or_reset_cleaner) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    EnableTwoReplayRounds();
    observed_.appReplay = true;
    MessageOfProfConfig config = ProfConfig::Instance().GetConfig();
    config.replayCount = EVENT_MAX_NUM / PMU_EVENT_MAX_NUM;
    ProfConfig::Instance().Init(config);
    MOCKER(&ProfConfig::GetInitReplayCount).stubs().will(returnValue(config.replayCount));
    ExpectNoCleanerUse();
    {
        ProfDataCollect collector;
        EXPECT_TRUE(collector.ProfData(nullptr, [] {
            ADD_FAILURE() << "No replay round should be launched";
            return true;
        }));
    }
    EXPECT_TRUE(observed_.allocations.empty());
    EXPECT_EQ(0U, observed_.cleanerResetCalls);
}

TEST_F(ClearL2ParamTest, collector_releases_buffers_at_end_of_each_kernel_replay) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    ProfDataCollect collector;
    for (uint32_t replay = 1; replay <= 2; ++replay) {
        observed_.expectedStream = reinterpret_cast<aclrtStream>(static_cast<uintptr_t>(replay));
        EXPECT_FALSE(collector.ProfData(observed_.expectedStream, [] {
            ADD_FAILURE() << "Replay should stop before launching the application kernel";
            return true;
        }));
        EXPECT_EQ(replay * 5, observed_.allocations.size());
        EXPECT_EQ(replay * 5, observed_.freeCalls);
        ExpectAllBuffersFreedOnce();
    }
}

TEST_F(ClearL2ParamTest, kernel_replay_shares_buffers_across_rounds_and_resets_before_return) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    EnableTwoReplayRounds();
    ProfDataCollect collector;
    uint32_t launches = 0;
    EXPECT_TRUE(collector.ProfData(nullptr, [&] {
        ++launches;
        EXPECT_EQ(5U, observed_.allocations.size());
        EXPECT_EQ(0U, observed_.freeCalls);
        return true;
    }));
    EXPECT_EQ(2U, launches);
    EXPECT_EQ(2U, observed_.clearCalls);
    EXPECT_EQ(5U, observed_.freeCalls);
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, range_replay_releases_buffers_after_failure_before_collector_destruction) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    ConfigureRangeReplay();
    ProfDataCollect collector(nullptr, false);
    // range 计数按线程持久化，使用独立线程避免影响其他用例的主线程计数。
    std::thread replay([&] { EXPECT_FALSE(collector.RangeReplay(nullptr, nullptr)); });
    replay.join();
    EXPECT_EQ(1U, observed_.clearCalls);
    EXPECT_EQ(5U, observed_.freeCalls);
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, range_replay_shares_buffers_across_rounds_and_resets_before_return) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    EnableTwoReplayRounds();
    ConfigureRangeReplay();
    MOCKER(aclmdlRIExecuteAsyncImplOrigin).expects(exactly(2)).will(invoke(ObserveModelExecution));
    ProfDataCollect collector(nullptr, false);
    std::thread replay([&] { EXPECT_TRUE(collector.RangeReplay(nullptr, nullptr)); });
    replay.join();
    EXPECT_EQ(5U, observed_.allocations.size());
    EXPECT_EQ(2U, observed_.clearCalls);
    EXPECT_EQ(5U, observed_.freeCalls);
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_does_not_clear_before_successful_preparation) {
    L2CacheCleaner cleaner;
    EXPECT_FALSE(cleaner.IsReady());
    EXPECT_FALSE(cleaner.Clear());
    EXPECT_TRUE(observed_.allocations.empty());
    EXPECT_EQ(0U, observed_.clearCalls);
}

TEST_F(ClearL2ParamTest, cleaner_owns_buffers_until_destruction_and_reuses_them_for_clearing) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    observed_.expectedStream = reinterpret_cast<aclrtStream>(0x1234);
    {
        L2CacheCleaner cleaner;
        ASSERT_TRUE(cleaner.Prepare(observed_.expectedStream, 7));
        EXPECT_TRUE(cleaner.IsReady());
        EXPECT_EQ(0U, observed_.freeCalls);
        EXPECT_EQ(1U, observed_.hostFreeCalls);
        EXPECT_TRUE(cleaner.Clear());
        EXPECT_TRUE(cleaner.Clear());
        EXPECT_EQ(5U, observed_.allocations.size());
        EXPECT_EQ(2U, observed_.clearCalls);
        EXPECT_EQ(2U, observed_.prefetchCalls);
        EXPECT_EQ(0U, observed_.freeCalls);
    }
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_releases_partial_preparation_after_each_device_allocation_failure) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    for (size_t failedAllocation = 1; failedAllocation <= 5; ++failedAllocation) {
        SCOPED_TRACE(failedAllocation);
        observed_ = ClearL2Observation{};
        observed_.failAllocationOn = failedAllocation;
        // 只有第 5 次设备分配（blockLen）失败时，才需要释放已申请的 host tiling。
        const uint32_t expectedHostFreeCalls = failedAllocation == 5 ? 1U : 0U;
        {
            L2CacheCleaner cleaner;
            EXPECT_FALSE(cleaner.Prepare(nullptr, 7));
            EXPECT_FALSE(cleaner.IsReady());
            EXPECT_FALSE(cleaner.Clear());
            EXPECT_EQ(failedAllocation - 1, observed_.freeCalls);
            EXPECT_EQ(expectedHostFreeCalls, observed_.hostFreeCalls);
            ExpectAllBuffersFreedOnce();
        }
        EXPECT_EQ(expectedHostFreeCalls, observed_.hostFreeCalls);
        ExpectAllBuffersFreedOnce();
    }
}

TEST_F(ClearL2ParamTest, cleaner_releases_partial_preparation_when_host_allocation_fails) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    observed_.failHostAllocation = true;
    {
        L2CacheCleaner cleaner;
        EXPECT_FALSE(cleaner.Prepare(nullptr, 7));
        EXPECT_FALSE(cleaner.IsReady());
        EXPECT_FALSE(cleaner.Clear());
        EXPECT_EQ(4U, observed_.freeCalls);
        ExpectAllBuffersFreedOnce();
    }
    EXPECT_EQ(0U, observed_.hostFreeCalls);
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_checks_host_free_result_without_changing_preparation_behavior) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    const std::string failureLog = "[WARN] aclrt API call aclrtFreeHostImpl() failed. error code: " +
        std::to_string(ACL_ERROR_INTERNAL_ERROR);
    for (bool failHostFree : {false, true}) {
        SCOPED_TRACE(testing::Message() << "failHostFree=" << failHostFree);
        for (bool failTilingCopy : {false, true}) {
            SCOPED_TRACE(testing::Message() << "failTilingCopy=" << failTilingCopy);
            observed_ = ClearL2Observation{};
            observed_.failHostFree = failHostFree;
            observed_.failHostToDeviceCopy = failTilingCopy;
            {
                L2CacheCleaner cleaner;
                testing::internal::CaptureStdout();
                const bool prepared = cleaner.Prepare(nullptr, 7);
                const std::string output = testing::internal::GetCapturedStdout();
                EXPECT_EQ(!failTilingCopy, prepared);
                EXPECT_EQ(!failTilingCopy, cleaner.IsReady());
                EXPECT_EQ(!failTilingCopy, cleaner.Clear());
                const bool expectWarning = failHostFree && InjectLogger::Instance().GetLogLv() <= LogLv::WARN;
                EXPECT_EQ(expectWarning, output.find(failureLog) != std::string::npos) << output;
            }
            EXPECT_EQ(1U, observed_.hostFreeCalls);
            ExpectAllBuffersFreedOnce();
        }
    }
}

TEST_F(ClearL2ParamTest, cleaner_releases_all_buffers_when_tiling_copy_fails) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    observed_.failHostToDeviceCopy = true;
    {
        L2CacheCleaner cleaner;
        EXPECT_FALSE(cleaner.Prepare(nullptr, 7));
        EXPECT_FALSE(cleaner.IsReady());
        EXPECT_FALSE(cleaner.Clear());
        EXPECT_EQ(5U, observed_.freeCalls);
        EXPECT_EQ(1U, observed_.hostFreeCalls);
        ExpectAllBuffersFreedOnce();
    }
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_releases_all_buffers_when_tiling_synchronization_fails) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    observed_.failSyncOn = 1;
    {
        L2CacheCleaner cleaner;
        EXPECT_FALSE(cleaner.Prepare(nullptr, 7));
        EXPECT_FALSE(cleaner.IsReady());
        EXPECT_FALSE(cleaner.Clear());
        EXPECT_EQ(5U, observed_.freeCalls);
        EXPECT_EQ(1U, observed_.hostFreeCalls);
        ExpectAllBuffersFreedOnce();
    }
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_can_retry_preparation_after_failure) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    observed_.failAllocationOn = 3;
    {
        L2CacheCleaner cleaner;
        EXPECT_FALSE(cleaner.Prepare(nullptr, 7));
        EXPECT_TRUE(cleaner.Prepare(nullptr, 7));
        EXPECT_TRUE(cleaner.IsReady());
        EXPECT_TRUE(cleaner.Clear());
        EXPECT_EQ(2U, observed_.freeCalls);
    }
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_releases_previous_buffers_before_preparing_again) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    {
        L2CacheCleaner cleaner;
        ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
        ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
        EXPECT_EQ(5U, observed_.freeCalls);
        EXPECT_TRUE(cleaner.Clear());
    }
    EXPECT_EQ(10U, observed_.freeCalls);
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_blocks_preparation_until_failed_buffer_is_released) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    for (size_t failedBuffer = 0; failedBuffer < 5; ++failedBuffer) {
        SCOPED_TRACE(failedBuffer);
        observed_ = ClearL2Observation{};
        {
            L2CacheCleaner cleaner;
            ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
            observed_.failFreeBuffer = observed_.buffers[failedBuffer];
            observed_.freeFailuresRemaining = 1;
            EXPECT_FALSE(cleaner.Prepare(nullptr, 7));
            EXPECT_FALSE(cleaner.IsReady());
            EXPECT_FALSE(cleaner.Clear());
            EXPECT_EQ(5U, observed_.allocationAttempts);
            EXPECT_EQ(5U, observed_.allocations.size());
            EXPECT_EQ(1U, observed_.hostFreeCalls);
            EXPECT_EQ(4U, observed_.freeCalls);
            ASSERT_EQ(5U, observed_.freeAttempts.size());
            EXPECT_EQ(0, std::count(observed_.freedBuffers.begin(), observed_.freedBuffers.end(),
                observed_.failFreeBuffer));

            cleaner.Reset();
            ASSERT_EQ(6U, observed_.freeAttempts.size());
            EXPECT_EQ(observed_.failFreeBuffer, observed_.freeAttempts.back());
            ExpectAllBuffersFreedOnce();
            ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
            EXPECT_TRUE(cleaner.IsReady());
            EXPECT_TRUE(cleaner.Clear());
            EXPECT_EQ(10U, observed_.allocations.size());
        }
        EXPECT_EQ(11U, observed_.freeAttempts.size());
        ExpectAllBuffersFreedOnce();
    }
}

TEST_F(ClearL2ParamTest, cleaner_keeps_blocking_preparation_while_release_fails) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    {
        L2CacheCleaner cleaner;
        ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
        observed_.failFreeBuffer = observed_.buffers[0];
        observed_.freeFailuresRemaining = 3;
        for (size_t attempt = 0; attempt < 3; ++attempt) {
            SCOPED_TRACE(attempt);
            EXPECT_FALSE(cleaner.Prepare(nullptr, 7));
            EXPECT_FALSE(cleaner.IsReady());
            EXPECT_FALSE(cleaner.Clear());
            EXPECT_EQ(5U, observed_.allocationAttempts);
            EXPECT_EQ(5U, observed_.allocations.size());
            EXPECT_EQ(4U, observed_.freeCalls);
            ASSERT_EQ(5U + attempt, observed_.freeAttempts.size());
            EXPECT_EQ(attempt + 1, static_cast<size_t>(std::count(observed_.freeAttempts.begin(),
                observed_.freeAttempts.end(), observed_.failFreeBuffer)));
        }
        // Prepare 的下一次 Reset 释放成功后，才允许重新申请全部缓冲区。
        ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
        EXPECT_TRUE(cleaner.IsReady());
        EXPECT_EQ(8U, observed_.freeAttempts.size());
        EXPECT_EQ(5U, observed_.freeCalls);
        EXPECT_EQ(10U, observed_.allocationAttempts);
        EXPECT_EQ(10U, observed_.allocations.size());
    }
    EXPECT_EQ(13U, observed_.freeAttempts.size());
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_destructor_retries_failed_partial_preparation_cleanup) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    observed_.failAllocationOn = 3;
    observed_.failFreeBuffer = observed_.buffers[0];
    observed_.freeFailuresRemaining = 1;
    {
        L2CacheCleaner cleaner;
        EXPECT_FALSE(cleaner.Prepare(nullptr, 7));
        EXPECT_FALSE(cleaner.IsReady());
        EXPECT_FALSE(cleaner.Clear());
        EXPECT_EQ(2U, observed_.allocations.size());
        EXPECT_EQ(1U, observed_.freeCalls);
        EXPECT_EQ(2U, observed_.freeAttempts.size());
    }
    ASSERT_EQ(3U, observed_.freeAttempts.size());
    EXPECT_EQ(observed_.failFreeBuffer, observed_.freeAttempts.back());
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_reset_is_idempotent_and_invalidates_readiness) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    L2CacheCleaner cleaner;
    cleaner.Reset();
    ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
    cleaner.Reset();
    EXPECT_FALSE(cleaner.IsReady());
    EXPECT_FALSE(cleaner.Clear());
    cleaner.Reset();
    EXPECT_EQ(5U, observed_.freeCalls);
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_concurrent_resets_free_each_buffer_once) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    L2CacheCleaner cleaner;
    ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
    std::promise<void> start;
    auto ready = start.get_future().share();
    std::vector<std::thread> workers;
    for (size_t i = 0; i < 8; ++i) {
        workers.emplace_back([&cleaner, ready] {
            ready.wait();
            cleaner.Reset();
        });
    }
    start.set_value();
    for (auto &worker : workers) {
        worker.join();
    }
    EXPECT_FALSE(cleaner.IsReady());
    EXPECT_FALSE(cleaner.Clear());
    EXPECT_EQ(5U, observed_.freeCalls);
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_reports_clear_kernel_failure) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    observed_.failClearKernel = true;
    {
        L2CacheCleaner cleaner;
        ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
        EXPECT_FALSE(cleaner.Clear());
        EXPECT_EQ(1U, observed_.syncCalls);
    }
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_reports_device_copy_failure) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    observed_.failDeviceCopy = true;
    {
        L2CacheCleaner cleaner;
        ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
        EXPECT_FALSE(cleaner.Clear());
        EXPECT_EQ(0U, observed_.prefetchCalls);
    }
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_reports_synchronization_failure_after_device_copy) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    observed_.failSyncOn = 2;
    {
        L2CacheCleaner cleaner;
        ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
        EXPECT_FALSE(cleaner.Clear());
        EXPECT_EQ(0U, observed_.prefetchCalls);
    }
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_reports_prefetch_failure) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    observed_.failPrefetch = true;
    {
        L2CacheCleaner cleaner;
        ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
        EXPECT_FALSE(cleaner.Clear());
        EXPECT_EQ(1U, observed_.prefetchCalls);
    }
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_reports_synchronization_failure_after_prefetch) {
    DeviceContext::Instance(0).SetSocVersion("Ascend910B4");
    observed_.failSyncOn = 3;
    {
        L2CacheCleaner cleaner;
        ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
        EXPECT_FALSE(cleaner.Clear());
        EXPECT_EQ(1U, observed_.prefetchCalls);
    }
    ExpectAllBuffersFreedOnce();
}

TEST_F(ClearL2ParamTest, cleaner_skips_prefetch_on_310p_and_a5) {
    for (const char *soc : {"Ascend310P3", "Ascend950DT_9571"}) {
        SCOPED_TRACE(soc);
        DeviceContext::Instance(0).SetSocVersion(soc);
        observed_ = ClearL2Observation{};
        {
            L2CacheCleaner cleaner;
            ASSERT_TRUE(cleaner.Prepare(nullptr, 7));
            EXPECT_TRUE(cleaner.Clear());
            EXPECT_EQ(0U, observed_.prefetchCalls);
        }
        ExpectAllBuffersFreedOnce();
    }
}

TEST_F(ClearL2ParamTest, cleaner_does_not_allocate_for_an_unrecognized_soc) {
    MOCKER(&aclrtGetDeviceInfoImplOrigin).expects(never());
    DeviceContext::Instance(0).SetSocVersion("UnknownSoc");
    L2CacheCleaner cleaner;
    EXPECT_FALSE(cleaner.Prepare(nullptr, 7));
    EXPECT_FALSE(cleaner.IsReady());
    EXPECT_TRUE(observed_.allocations.empty());
    EXPECT_TRUE(observed_.attributes.empty());
}

TEST_F(ClearL2ParamTest, cleaner_propagates_empty_kernel_success) {
    MOCKER(&DFXKernelLauncher::CallEmptyKernel).expects(once()).will(returnValue(true));
    EXPECT_TRUE(L2CacheCleaner::CallEmptyKernel(nullptr));
}

TEST_F(ClearL2ParamTest, cleaner_propagates_empty_kernel_failure) {
    MOCKER(&DFXKernelLauncher::CallEmptyKernel).expects(once()).will(returnValue(false));
    EXPECT_FALSE(L2CacheCleaner::CallEmptyKernel(nullptr));
}

} // namespace
