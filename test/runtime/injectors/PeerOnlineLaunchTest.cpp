/* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved. */
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <gtest/gtest.h>
#include "mockcpp/mockcpp.hpp"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "runtime/inject_helpers/ThreadContext.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "runtime/inject_helpers/LaunchContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/ConfigManager.h"
#include "runtime/inject_helpers/LaunchManager.h"
#include "runtime/inject_helpers/ArgsManager.h"
#include "runtime/inject_helpers/InteractHelper.h"
#include "runtime/RuntimeOrigin.h"
#include "core/HijackedLayerManager.h"
#define private public
#include "runtime/inject_helpers/LaunchInitFinalize.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/DevMemManager.h"
#undef private

namespace {
int copyNumber;
int failCopy;
bool failMemset;
int frees;
aclError Copy(void *dst, size_t capacity, const void *src, size_t size, aclrtMemcpyKind) {
    if (++copyNumber == failCopy || size > capacity) {
        return ACL_ERROR_INTERNAL_ERROR;
    }
    std::memcpy(dst, src, size);
    return ACL_SUCCESS;
}
aclError Fill(void *dst, size_t capacity, int32_t value, size_t size) {
    if (failMemset || size > capacity) {
        return ACL_ERROR_INTERNAL_ERROR;
    }
    std::memset(dst, value, size);
    return ACL_SUCCESS;
}
aclError Allocate(void **ptr, size_t size, aclrtMemMallocPolicy) {
    *ptr = std::malloc(size);
    return *ptr ? ACL_SUCCESS : ACL_ERROR_BAD_ALLOC;
}
aclError Release(void *ptr) {
    ++frees;
    std::free(ptr);
    return ACL_SUCCESS;
}
aclError Device(int32_t *id) {
    *id = 640;
    return ACL_SUCCESS;
}
}

class PeerOnlineLaunch : public testing::Test {
protected:
    SanitizerLaunchInit launch;
    void SetUp() override {
        copyNumber = 0;
        failCopy = -1;
        failMemset = false;
        frees = 0;
        MOCKER(aclrtGetDeviceImplOrigin).stubs().will(invoke(Device));
        ThreadContext::Instance().SetDeviceId(640);
        MemoryManage::Instance().Reset();
        KernelContext::Instance().GetOpMemInfo() = {};
        MOCKER(aclrtMemcpyImplOrigin).stubs().will(invoke(Copy));
        MOCKER(aclrtMemsetImplOrigin).stubs().will(invoke(Fill));
        MOCKER(aclrtMallocImplOrigin).stubs().will(invoke(Allocate));
        MOCKER(aclrtFreeImplOrigin).stubs().will(invoke(Release));
        DevMemManager::Instance().Free();
        frees = 0;
        DevMemManager::Instance().SetSkipKernelFlag(false);
        DevMemManager::Instance().SetMemoryInitFlag(false);
    }
    void TearDown() override {
        DevMemManager::Instance().Free();
        MemoryManage::Instance().Reset();
        GlobalMockObject::verify();
    }
    void Prepare(uint32_t locals, uint32_t peers, uint32_t blocks) {
        launch.hostMemoryNum_ = locals;
        for (uint32_t i = 0; i < locals; ++i) {
            MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(0x1000 + i * 128, MemInfoSrc::ACL, 64);
        }
        for (uint32_t i = 0; i < peers; ++i) {
            launch.peerMemories_.push_back({0x8000 + i * 128, 64, 3, static_cast<int32_t>(641 + i % 2)});
        }
        auto &head = launch.globalHead_;
        head.supportSimt = true;
        head.checkParms.checkBlockId = CHECK_ALL_BLOCK;
        head.checkParms.cacheSize = 4;
        head.kernelInfo.totalBlockDim = blocks;
        head.kernelInfo.totalCacheSize = blocks * 4;
        const uint64_t raw = sizeof(RecordBlockHead) + locals * sizeof(HostMemoryInfo) + peers * sizeof(PeerMemoryInfo);
        head.offsetInfo.blockHeadSize = (raw + 63) / 64 * 64;
        head.offsetInfo.simtErrorInfo.offset = head.offsetInfo.blockHeadSize;
        head.offsetInfo.simtErrorInfo.size = 64;
    }
};

TEST_F(PeerOnlineLaunch, RealHostWriterKeepsAllBlocksRangesAndPointersInsideTheirBlock) {
    for (uint32_t locals : {0U, 1U, 3U}) {
        for (uint32_t peers : {0U, 1U, 17U}) {
            SCOPED_TRACE(std::to_string(locals) + "/" + std::to_string(peers));
            MemoryManage::Instance().Reset();
            launch = SanitizerLaunchInit{};
            Prepare(locals, peers, 3);
            const size_t stride = launch.globalHead_.offsetInfo.blockHeadSize + 4 * MB_TO_BYTES;
            const size_t size = sizeof(RecordGlobalHead) + 3 * stride;
            std::vector<uint8_t> bytes(size + 64, 0xa5);
            ASSERT_TRUE(launch.BlockHeadsH2D(bytes.data()));
            for (size_t block = 0; block < 3; ++block) {
                auto *base = bytes.data() + sizeof(RecordGlobalHead) + block * stride;
                auto *head = reinterpret_cast<RecordBlockHead *>(base);
                EXPECT_EQ(head->hostMemoryNum, locals);
                EXPECT_EQ(head->peerMemoryNum, peers);
                EXPECT_EQ(head->hostMemoryInfoPtr, locals ? reinterpret_cast<HostMemoryInfo *>(head + 1) : nullptr);
                EXPECT_EQ(head->peerMemoryInfoPtr,
                    peers ? reinterpret_cast<PeerMemoryInfo *>(
                                base + sizeof(RecordBlockHead) + locals * sizeof(HostMemoryInfo))
                          : nullptr);
                for (uint32_t i = 0; i < peers; ++i) {
                    EXPECT_EQ(head->peerMemoryInfoPtr[i].addr, launch.peerMemories_[i].addr);
                    EXPECT_EQ(head->peerMemoryInfoPtr[i].deviceId, launch.peerMemories_[i].deviceId);
                    EXPECT_EQ(head->peerMemoryInfoPtr[i].permission, launch.peerMemories_[i].permission);
                }
                EXPECT_EQ(base[stride - 1], 0xa5); // 没有覆盖下一个 block 的缓存边界。
            }
            EXPECT_TRUE(std::all_of(bytes.begin() + size, bytes.end(), [](uint8_t b) { return b == 0xa5; }));
        }
    }
}

TEST_F(PeerOnlineLaunch, EachH2dFailureAndSimtMemsetFailurePropagatesAndReleasesAllocation) {
    Prepare(1, 1, 2);
    MOCKER(&SanitizerLaunchInit::AssignGlobalHead).stubs().will(returnValue(true));
    // global、block0、本地0、远端0、block1、本地1、远端1。
    for (int failure = 1; failure <= 7; ++failure) {
        SCOPED_TRACE(failure);
        copyNumber = 0;
        failCopy = failure;
        const int before = frees;
        EXPECT_EQ(launch.Process(), nullptr);
        EXPECT_EQ(frees, before + 1);
        EXPECT_EQ(DevMemManager::Instance().memPtr_, nullptr);
        EXPECT_FALSE(DevMemManager::Instance().IsMemoryInit());
    }
    failCopy = -1;
    failMemset = true;
    EXPECT_EQ(launch.Process(), nullptr);
    EXPECT_EQ(DevMemManager::Instance().memPtr_, nullptr);
    failMemset = false;
    EXPECT_NE(launch.Process(), nullptr); // 失败后可重新初始化，不保留半份快照。
    EXPECT_TRUE(DevMemManager::Instance().IsMemoryInit());
}

TEST_F(PeerOnlineLaunch, AllocationFailureDoesNotCopyOrMarkInitialized) {
    Prepare(1, 1, 1);
    MOCKER(&SanitizerLaunchInit::AssignGlobalHead).stubs().will(returnValue(true));
    MOCKER(&DevMemManager::MallocMemory).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    EXPECT_EQ(launch.Process(), nullptr);
    EXPECT_EQ(copyNumber, 0);
    EXPECT_FALSE(DevMemManager::Instance().IsMemoryInit());
}

TEST_F(PeerOnlineLaunch, InitRefreshesAuthorizedSnapshotWithoutChangingCurrentDevice) {
    auto &state = PeerAccessState::Instance();
    auto &memory = MemoryManage::Instance(641);
    state.Apply(640, -1, PeerAccessOp::RESET);
    memory.Reset();
    MOCKER(&SanitizerLaunchInit::InitKernelType).stubs();
    memory.CacheMemory<MemoryOpType::MALLOC>(0x8000, MemInfoSrc::ACL, 128);
    launch.Init(3);
    EXPECT_TRUE(launch.peerMemories_.empty());
    state.Apply(640, 641, PeerAccessOp::ENABLE);
    launch.Init(3);
    ASSERT_EQ(launch.peerMemories_.size(), 1);
    const auto first = launch.peerMemories_;
    memory.CacheMemory<MemoryOpType::FREE>(0x8000, MemInfoSrc::ACL);
    memory.CacheMemory<MemoryOpType::MALLOC>(0x8000, MemInfoSrc::ACL, 32);
    launch.Init(3);
    ASSERT_EQ(launch.peerMemories_.size(), 1);
    EXPECT_EQ(launch.peerMemories_[0].size, 32);
    EXPECT_EQ(first[0].size, 128);
    state.Apply(640, 641, PeerAccessOp::DISABLE);
    launch.Init(3);
    EXPECT_TRUE(launch.peerMemories_.empty());
    EXPECT_EQ(ThreadContext::Instance().GetDeviceId(), 640);
    memory.Reset();
    state.Apply(640, -1, PeerAccessOp::RESET);
}

TEST_F(PeerOnlineLaunch, UnrepresentableHeaderSizeMustBeRejectedBeforeAllocation) {
    // 只设置数量，不创建几十 GB 的 vector，也不申请设备内存。
    MOCKER(GetDeviceTypeBySocVersion).stubs().will(returnValue(DeviceType::ASCEND_950DT_950x));
    SanitizerConfig config{};
    config.cacheSize = 1;
    config.checkBlockId = CHECK_ALL_BLOCK;
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(config));
    launch.blockDim_ = 1;
    launch.hostMemoryNum_ = UINT32_MAX;
    EXPECT_FALSE(launch.AssignGlobalHead()); // blockHeadSize 是 uint32_t，不能截断后报告成功。
}

TEST_F(PeerOnlineLaunch, SelectedBlockUsesItsOwnRangesAndLeavesOtherSimtAreasUntouched) {
    MOCKER(GetDeviceTypeBySocVersion).stubs().will(returnValue(DeviceType::ASCEND_950DT_950x));
    Prepare(1, 2, 3);
    auto &global = launch.globalHead_;
    global.checkParms.cacheSize = 4;
    global.checkParms.checkBlockId = 1;
    const size_t small = global.offsetInfo.blockHeadSize + MB_TO_BYTES;
    const size_t large = global.offsetInfo.blockHeadSize + 4 * MB_TO_BYTES;
    std::vector<uint8_t> bytes(sizeof(RecordGlobalHead) + 2 * small + large, 0xa5);
    ASSERT_TRUE(launch.BlockHeadsH2D(bytes.data()));
    size_t offset = sizeof(RecordGlobalHead);
    for (size_t b = 0; b < 3; ++b) {
        auto *head = reinterpret_cast<RecordBlockHead *>(bytes.data() + offset);
        ASSERT_EQ(head->peerMemoryNum, 2);
        EXPECT_EQ(head->peerMemoryInfoPtr[1].addr, launch.peerMemories_[1].addr);
        EXPECT_EQ(bytes[offset + global.offsetInfo.simtErrorInfo.offset], b == 1 ? 0 : 0xa5);
        offset += b == 1 ? large : small;
    }
    EXPECT_EQ(offset, bytes.size());
}

// 使用真实初始化和结束处理，四类启动的能力、快照和参数区应各自符合协议
TEST_F(PeerOnlineLaunch, C220And950OrdinaryVfAndSimtLaunchLifecycle) {
    SanitizerConfig config{};
    config.defaultCheck = true;
    config.memCheck = true;
    // 32 MB 能容纳 2048 个 SIMT 线程头，使用合法的在线检测缓存配置
    config.cacheSize = 32;
    config.checkBlockId = CHECK_ALL_BLOCK;
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(config));
    MOCKER(rtGetL2CacheOffsetOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    static DeviceType deviceType;
    MOCKER(GetDeviceTypeBySocVersion).stubs().will(invoke(+[](const std::string &) { return deviceType; }));
    MOCKER(&SanitizerLaunchInit::InitKernelType).stubs().will(invoke(+[](SanitizerLaunchInit *self) {
        self->kernelType_ = KernelType::AIVEC;
    }));
    // 只替代记录传输，初始化和参数区的注册/释放仍走真实 Host 实现
    MOCKER((InteractHelper::Interact<std::string, KernelRecordResponse, 600>)).stubs().will(returnValue(true));
    MOCKER((InteractHelper::Notify<std::string>)).stubs();
    static std::vector<std::pair<uint64_t, uint64_t>> allocations;
    static std::vector<uint64_t> released;
    MOCKER(ReportMalloc).stubs().will(invoke(+[](uint64_t addr, uint64_t size, MemInfoSrc, MemInfoDesc, uint64_t) {
        allocations.emplace_back(addr, size);
    }));
    MOCKER(ReportMemset).stubs();
    MOCKER(ReportFree).stubs().will(invoke(+[](uint64_t addr, MemInfoSrc, MemInfoDesc, uint64_t) {
        released.push_back(addr);
    }));
    for (int entry = 0; entry < 4; ++entry) {
        SCOPED_TRACE(entry);
        allocations.clear();
        released.clear();
        deviceType = entry == 0 ? DeviceType::ASCEND_910B1 : DeviceType::ASCEND_950DT_950x;
        LayerGuard layer(HijackedLayerManager::Instance(),
            entry == 3 ? "aclrtLaunchSIMTKernelWithHostArgsImpl" : "aclrtLaunchKernelImpl");
        launch = SanitizerLaunchInit{};
        auto &state = PeerAccessState::Instance();
        state.Apply(640, -1, PeerAccessOp::RESET);
        MemoryManage::Instance().Reset();
        MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(0x1000, MemInfoSrc::ACL, 64);
        MemoryManage::Instance(641).Reset();
        MemoryManage::Instance(641).CacheMemory<MemoryOpType::MALLOC>(0x8000, MemInfoSrc::ACL, 64);
        state.Apply(640, 641, PeerAccessOp::ENABLE);
        ArgsManager::Instance().SetArgsSize(64);
        LaunchManager::Local().SetSimtUbDynamicSize(entry == 2 ? 129 : 0);
        launch.Init(1);
        ASSERT_EQ(launch.peerMemories_.size(), 1U);
        auto *bytes = launch.Process();
        ASSERT_NE(bytes, nullptr);
        auto *head = reinterpret_cast<RecordGlobalHead *>(bytes);
        EXPECT_EQ(head->supportSimt, entry != 0);
        EXPECT_EQ(head->kernelInfo.kernelType, KernelType::AIVEC);
        EXPECT_EQ(head->simtInfo.ubDynamicSize, entry == 2 ? 256U : 0U);
        auto *block = reinterpret_cast<RecordBlockHead *>(head + 1);
        ASSERT_EQ(block->paraBase.size, 64U);
        ASSERT_EQ(block->peerMemoryNum, 1U);
        if (entry != 0) {
            EXPECT_EQ(block->peerMemoryInfoPtr[0].addr, 0x8000U);
            EXPECT_EQ(block->peerMemoryInfoPtr[0].permission, 3U);
        }
        // 模拟 kernel 写回的参数基址和一条普通记录，使 finalize 实际执行参数区生命周期
        block->paraBase.addr = 0x4000;
        block->recordCount = block->recordWriteCount = 1;
        block->offset = block->writeOffset = 4U;
        {
            SanitizerLaunchFinalize finalize;
            finalize.Init(bytes, 1);
            finalize.Process();
        }
        const std::vector<std::pair<uint64_t, uint64_t>> expected = entry == 3
            ? std::vector<std::pair<uint64_t, uint64_t>>{{0x3fe0, 32}, {0x4000, 64}}
            : std::vector<std::pair<uint64_t, uint64_t>>{{0x4000, 64}};
        EXPECT_EQ(allocations, expected);
        EXPECT_EQ(released, entry == 3 ? (std::vector<uint64_t>{0x3fe0, 0x4000}) : (std::vector<uint64_t>{0x4000}));
        EXPECT_FALSE(DevMemManager::Instance().IsMemoryInit());
        const int before = frees;
        DevMemManager::Instance().Free();
        EXPECT_EQ(frees, before + 1);
        EXPECT_EQ(DevMemManager::Instance().memPtr_, nullptr);
        MemoryManage::Instance(641).Reset();
        state.Apply(640, -1, PeerAccessOp::RESET);
    }
    ArgsManager::Instance().SetArgsSize(0);
    LaunchManager::Local().SetSimtUbDynamicSize(0);
}
