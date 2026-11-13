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

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "acl_rt_impl/AscendclImplOrigin.h"
#include "mockcpp/mockcpp.hpp"
#include "profapi/ProfOriginal.h"
#include "runtime/inject_helpers/DFXKernelLauncher.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/MemoryContext.h"
#include "runtime/inject_helpers/MsTx.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "runtime/inject_helpers/ThreadContext.h"
#include "utils/FileSystem.h"

namespace {

struct ClearL2Observation {
    std::vector<size_t> allocations;
    uint8_t buffers[5][32]{};
    uint64_t hostTiling = 0;
    uint64_t copiedTiling = 0;
    uint32_t blockDim = 0;
    uint32_t clearCalls = 0;
    uint32_t freeCalls = 0;
    int64_t l2Bytes = 100663296;
    int64_t cores = 20;
    aclrtDevAttr failingAttribute = ACL_DEV_ATTR_L2_CACHE_SIZE;
    bool failQuery = false;
    std::vector<aclrtDevAttr> attributes;
};

ClearL2Observation *observation = nullptr;

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
    const size_t index = observation->allocations.size();
    if (index >= 5) {
        ADD_FAILURE() << "Unexpected device allocation";
        return ACL_ERROR_INTERNAL_ERROR;
    }
    observation->allocations.push_back(size);
    // 模拟设备地址，不实际分配数百 MiB；设备拷贝和 kernel 调用均由测试观察。
    *ptr = observation->buffers[index];
    return ACL_SUCCESS;
}

aclError CopyTiling(void *, size_t, const void *src, size_t count, aclrtMemcpyKind kind, aclrtStream) {
    if (kind == ACL_MEMCPY_HOST_TO_DEVICE) {
        EXPECT_EQ(sizeof(uint64_t), count);
        std::memcpy(&observation->copiedTiling, src, sizeof(uint64_t));
    }
    return ACL_SUCCESS;
}

aclError AllocateHostTiling(void **ptr, size_t size) {
    EXPECT_EQ(sizeof(uint64_t), size);
    *ptr = &observation->hostTiling;
    return ACL_SUCCESS;
}

bool ObserveClearKernel(DFXKernelLauncher *, uint32_t blockDim, void *, std::vector<void *> &args) {
    EXPECT_EQ(2U, args.size());
    if (args.size() == 2) {
        EXPECT_EQ(static_cast<void *>(observation->buffers[3]), args[0]);
        EXPECT_EQ(static_cast<void *>(observation->buffers[4]), args[1]);
    }
    observation->blockDim = blockDim;
    ++observation->clearCalls;
    return true;
}

aclError ObserveFree(void *) {
    ++observation->freeCalls;
    return ACL_SUCCESS;
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
        MOCKER(&ProfConfig::IsAppReplay).stubs().will(returnValue(false));
        MOCKER(&ProfConfig::IsRangeReplay).stubs().will(returnValue(false));
        MOCKER(&ProfConfig::IsSimulatorLaunchedByDevice).stubs().will(returnValue(false));
        MOCKER(&ProfConfig::GetOutputPathFromRemote).stubs().will(returnValue(path_));
        MOCKER(&MsTx::IsInMstxRange).stubs().will(returnValue(false));
        // 工具线程索引为0，采集对象保存设备7，属性查询必须使用后者。
        MOCKER(&ThreadContext::GetDeviceId).stubs().will(returnValue(0));
        MOCKER(DeviceContext::GetRunningDeviceId).stubs().will(returnValue(int32_t{7}));
        MOCKER(aclrtGetDeviceImplOrigin).expects(never());
        MOCKER(&KernelContext::GetMC2Flag).stubs().will(returnValue(false));
        MOCKER(&KernelContext::GetLcclFlag).stubs().will(returnValue(false));
        MOCKER(&MemoryContext::Backup).stubs().will(returnValue(true));
        // 参数准备及清 L2 后，在输入恢复处停止，不进入 PMU 驱动/读线程。
        MOCKER(&MemoryContext::Restore).stubs().will(returnValue(false));
        MOCKER(profSetProfCommandOrigin).stubs().will(returnValue(ACL_SUCCESS));
        MOCKER(aclrtSynchronizeStreamImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
        MOCKER(aclrtCmoAsyncImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
        MOCKER(aclrtMallocImplOrigin).stubs().will(invoke(AllocateDeviceBuffer));
        MOCKER(aclrtMallocHostImplOrigin).stubs().will(invoke(AllocateHostTiling));
        MOCKER(aclrtFreeHostImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
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
    }

    void ExpectPrepared(uint64_t bytesPerCore, uint32_t cores) {
        const size_t bytes = bytesPerCore * cores;
        EXPECT_EQ((std::vector<size_t>{bytes, bytes, bytes, bytes, 32}), observed_.allocations);
        EXPECT_EQ(bytesPerCore, observed_.copiedTiling);
        EXPECT_EQ(cores, observed_.blockDim);
        EXPECT_EQ(1U, observed_.clearCalls);
        EXPECT_EQ(5U, observed_.freeCalls);
    }

    ClearL2Observation observed_;
    MessageOfProfConfig previousConfig_{};
    std::string previousSoc_;
    std::string path_;
};

TEST_F(ClearL2ParamTest, known_soc_uses_calibrated_tiling_without_attribute_queries) {
    MOCKER(aclrtGetDeviceInfoImplOrigin).expects(never());
    Replay("Ascend910B4");
    ExpectPrepared(5242880, 20);
}

TEST_F(ClearL2ParamTest, unknown_soc_queries_target_device_and_aligns_tiling) {
    MOCKER(aclrtGetDeviceInfoImplOrigin).expects(exactly(2)).will(invoke(QueryTargetAttribute));
    Replay("Ascend910B9999");
    ExpectPrepared(5033472, 20);
    EXPECT_EQ(
        (std::vector<aclrtDevAttr>{ACL_DEV_ATTR_L2_CACHE_SIZE, ACL_DEV_ATTR_AICORE_CORE_NUM}), observed_.attributes);
}

TEST_F(ClearL2ParamTest, aligned_dynamic_tiling_does_not_add_an_extra_tile) {
    observed_.l2Bytes = 10240;
    MOCKER(aclrtGetDeviceInfoImplOrigin).expects(exactly(2)).will(invoke(QueryTargetAttribute));
    Replay("Ascend910_93xx");
    ExpectPrepared(512, 20);
}

TEST_F(ClearL2ParamTest, l2_query_failure_stops_before_core_query_or_buffer_preparation) {
    observed_.failQuery = true;
    MOCKER(aclrtGetDeviceInfoImplOrigin).expects(once()).will(invoke(QueryTargetAttribute));
    Replay("Ascend910B9999");
    EXPECT_TRUE(observed_.allocations.empty());
    EXPECT_EQ(0U, observed_.copiedTiling);
    EXPECT_EQ(0U, observed_.clearCalls);
    EXPECT_EQ((std::vector<aclrtDevAttr>{ACL_DEV_ATTR_L2_CACHE_SIZE}), observed_.attributes);
}

TEST_F(ClearL2ParamTest, core_query_failure_stops_before_buffer_preparation) {
    observed_.failQuery = true;
    observed_.failingAttribute = ACL_DEV_ATTR_AICORE_CORE_NUM;
    MOCKER(aclrtGetDeviceInfoImplOrigin).expects(exactly(2)).will(invoke(QueryTargetAttribute));
    Replay("Ascend950PR_95A9");
    EXPECT_TRUE(observed_.allocations.empty());
    EXPECT_EQ(0U, observed_.copiedTiling);
    EXPECT_EQ(0U, observed_.clearCalls);
    EXPECT_EQ(
        (std::vector<aclrtDevAttr>{ACL_DEV_ATTR_L2_CACHE_SIZE, ACL_DEV_ATTR_AICORE_CORE_NUM}), observed_.attributes);
}

} // namespace
