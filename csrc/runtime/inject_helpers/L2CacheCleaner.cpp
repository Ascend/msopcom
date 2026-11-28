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
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * ------------------------------------------------------------------------- */

#include "L2CacheCleaner.h"

#include <map>
#include <memory>
#include <string>
#include <vector>
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "DFXKernelLauncher.h"
#include "DeviceAttr.h"
#include "DeviceContext.h"
#include "utils/InjectLogger.h"

namespace {
struct L2CacheClearTiling {
    uint64_t clearSizePerCore; // B
    uint32_t aicCoreNum;
};

// 与 ClearL2Cache kernel 的 TILE_LENGTH 保持一致，避免尾部数据未被触碰。
constexpr uint64_t L2_CLEAR_TILE_LENGTH = 512;

uint64_t CalcClearSizePerCore(uint64_t l2CacheSize, uint32_t coreNum) {
    if (coreNum == 0) {
        return 0;
    }
    uint64_t perCore = (l2CacheSize + coreNum - 1) / coreNum;
    return (perCore + L2_CLEAR_TILE_LENGTH - 1) / L2_CLEAR_TILE_LENGTH * L2_CLEAR_TILE_LENGTH;
}

bool QueryClearL2Tiling(const std::string &socVersion, int32_t devId, L2CacheClearTiling &tiling) {
    if (ResolveProductSeriesBySocVersionPrefix(socVersion) == ChipProductType::UNKNOWN_PRODUCT_TYPE) {
        return false;
    }
    uint64_t l2CacheSize = 0;
    if (!DeviceAttr::QueryL2CacheSize(devId, l2CacheSize)) {
        WARN_LOG("Cannot get L2Cache size when clear L2Cache, socVersion=%s, deviceId=%d", socVersion.c_str(), devId);
        return false;
    }
    uint32_t aicCoreNum = 0;
    if (!DeviceAttr::QueryAicoreCoreNum(devId, aicCoreNum)) {
        WARN_LOG(
            "Cannot get aicore core num when clear L2Cache, socVersion=%s, deviceId=%d", socVersion.c_str(), devId);
        return false;
    }
    tiling.clearSizePerCore = CalcClearSizePerCore(l2CacheSize, aicCoreNum);
    tiling.aicCoreNum = aicCoreNum;
    if (tiling.clearSizePerCore == 0) {
        return false;
    }
    INFO_LOG("Automatic SoC adaptation: soc=%s, runtime L2 cache tiling: L2=%lu bytes, cores=%u, bytes/core=%lu, "
             "deviceId=%d.",
        socVersion.c_str(), static_cast<unsigned long>(l2CacheSize), aicCoreNum,
        static_cast<unsigned long>(tiling.clearSizePerCore), devId);
    return true;
}

const std::map<std::string, L2CacheClearTiling> L2_CACHE_CLEAR_TILING_MAP = {
    {"Ascend910B1", {8388608, 24}},
    {"Ascend910B2", {8388608, 24}},
    {"Ascend910B2C", {8388608, 24}},
    {"Ascend910B3", {10485760, 20}},
    {"Ascend910B4", {5242880, 20}},
    {"Ascend910B4-1", {10485760, 20}},
    {"Ascend910_9391", {8388608, 24}},
    {"Ascend910_9392", {8388608, 24}},
    {"Ascend910_9381", {8388608, 24}},
    {"Ascend910_9382", {8388608, 24}},
    {"Ascend910_9372", {10485760, 20}},
    {"Ascend910_9362", {10485760, 20}},
    {"Ascend910_9363", {10485760, 20}},
    {"Ascend310P1", {2097152, 10}},
    {"Ascend310P3", {2097152, 8}},
    {"Ascend310P5", {2097152, 8}},
    {"Ascend310P7", {2097152, 8}},
    {"Ascend950DT_950x", {4194304, 8}},
    {"Ascend950DT_950y", {4194304, 8}},
    {"Ascend950DT_9571", {4794880, 28}},
    {"Ascend950DT_9572", {4794880, 28}},
    {"Ascend950DT_9573", {4194304, 28}},
    {"Ascend950DT_9574", {4194304, 28}},
    {"Ascend950DT_9575", {4794880, 28}},
    {"Ascend950DT_9576", {4794880, 28}},
    {"Ascend950DT_9577", {4194304, 28}},
    {"Ascend950DT_9578", {4194304, 28}},
    {"Ascend950DT_9581", {4194304, 32}},
    {"Ascend950DT_9582", {4194304, 32}},
    {"Ascend950DT_9583", {3670016, 32}},
    {"Ascend950DT_9584", {3670016, 32}},
    {"Ascend950DT_9585", {4194304, 32}},
    {"Ascend950DT_9586", {4194304, 32}},
    {"Ascend950DT_9587", {3670016, 32}},
    {"Ascend950DT_9588", {3670016, 32}},
    {"Ascend950DT_9591", {3729920, 36}},
    {"Ascend950DT_9592", {3729920, 36}},
    {"Ascend950DT_9595", {3729920, 36}},
    {"Ascend950DT_9596", {3729920, 36}},
    {"Ascend950DT_95A1", {3728384, 36}},
    {"Ascend950DT_95A2", {3728384, 36}},
    {"Ascend950PR_950z", {2097152, 8}},
    {"Ascend950PR_9579", {4794880, 28}},
    {"Ascend950PR_957b", {4194304, 28}},
    {"Ascend950PR_957c", {4194304, 28}},
    {"Ascend950PR_957d", {3596800, 28}},
    {"Ascend950PR_9589", {4194304, 32}},
    {"Ascend950PR_958b", {3670016, 32}},
    {"Ascend950PR_9599", {3729920, 36}},
};

aclError CheckAclResult(aclError result, const char *apiName) {
    if (result != ACL_SUCCESS) {
        WARN_LOG("aclrt API call %s() failed. error code: %d", apiName, result);
    }
    return result;
}

void FreeBuffer(void *&buffer, const char *apiName) {
    if (buffer != nullptr && CheckAclResult(aclrtFreeImplOrigin(buffer), apiName) == ACL_SUCCESS) {
        buffer = nullptr;
    }
}
} // namespace

L2CacheCleaner::~L2CacheCleaner() {
    Reset();
}

bool L2CacheCleaner::Prepare(rtStream_t stream, int32_t deviceId) {
    Reset();
    // 释放失败时保留旧地址，禁止新分配覆盖，等待后续 Reset 重试。
    if (flushBuffer_ != nullptr || buffer_ != nullptr || cmoBuffer_ != nullptr ||
        l2Buffer_ != nullptr || blockLen_ != nullptr) {
        WARN_LOG("Cannot prepare L2CacheCleaner before previous buffers are released.");
        return false;
    }
    stream_ = stream;
    const std::string &socVersion = DeviceContext::Local().GetSocVersion();
    L2CacheClearTiling tiling{};
    auto it = L2_CACHE_CLEAR_TILING_MAP.find(socVersion);
    if (it != L2_CACHE_CLEAR_TILING_MAP.end()) {
        tiling = it->second;
    } else if (!QueryClearL2Tiling(socVersion, deviceId, tiling)) {
        WARN_LOG("Cannot get L2Cache info when clear L2Cache, socVersion=%s, deviceId=%d", socVersion.c_str(), deviceId);
        return false;
    }
    bufferSize_ = tiling.clearSizePerCore * tiling.aicCoreNum;
    if (!MallocBuffer()) {
        Reset();
        return false;
    }
    if (aclrtMallocImplOrigin(&l2Buffer_, bufferSize_, ACL_MEM_MALLOC_HUGE_FIRST) != ACL_SUCCESS) {
        WARN_LOG("Malloc buffer failed when clear L2Cache.");
        Reset();
        return false;
    }
    void *hostBlockLen = nullptr;
    if (aclrtMallocHostImplOrigin(&hostBlockLen, sizeof(uint64_t)) != ACL_SUCCESS) {
        WARN_LOG("Malloc host tiling info failed when clear L2Cache.");
        Reset();
        return false;
    }
    auto freeHostBlockLen = [](void *ptr) {
        CheckAclResult(aclrtFreeHostImplOrigin(ptr), "aclrtFreeHostImpl");
    };
    std::unique_ptr<void, decltype(freeHostBlockLen)> hostBlockLenDefer(hostBlockLen, freeHostBlockLen);
    *static_cast<uint64_t *>(hostBlockLen) = tiling.clearSizePerCore;
    // runtime 接口不会自动补齐到 32 字节，小块设备内存可能在访问时失败。
    if (aclrtMallocImplOrigin(&blockLen_, 32, ACL_MEM_MALLOC_HUGE_FIRST) != ACL_SUCCESS) {
        WARN_LOG("Malloc tiling info failed when clear L2Cache.");
        Reset();
        return false;
    }
    if (aclrtMemcpyAsyncImplOrigin(blockLen_, sizeof(uint64_t), hostBlockLen, sizeof(uint64_t),
            ACL_MEMCPY_HOST_TO_DEVICE, stream_) != ACL_SUCCESS ||
        aclrtSynchronizeStreamImplOrigin(stream_) != ACL_SUCCESS) {
        WARN_LOG("Memcpy tiling info failed when clear L2Cache.");
        Reset();
        return false;
    }
    blockDim_ = tiling.aicCoreNum;
    isReady_ = true;
    DEBUG_LOG("Prepare clear L2Cache param success, socVersion=%s, clearSizePerCore=%lu, aicCoreNum=%u.",
        socVersion.c_str(), static_cast<unsigned long>(tiling.clearSizePerCore), tiling.aicCoreNum);
    return true;
}

bool L2CacheCleaner::MallocBuffer() {
    if (CheckAclResult(aclrtMallocImplOrigin(&buffer_, bufferSize_, ACL_MEM_MALLOC_HUGE_FIRST),
            "buffer aclrtMallocImpl") != ACL_SUCCESS) {
        return false;
    }
    if (CheckAclResult(aclrtMallocImplOrigin(&flushBuffer_, bufferSize_, ACL_MEM_MALLOC_HUGE_FIRST),
            "flush buffer aclrtMallocImpl") != ACL_SUCCESS) {
        return false;
    }
    return CheckAclResult(aclrtMallocImplOrigin(&cmoBuffer_, bufferSize_, ACL_MEM_MALLOC_HUGE_FIRST),
        "cmoBuffer aclrtMallocImpl") == ACL_SUCCESS;
}

// 搬运 buffer 到 flushBuffer，并按芯片能力预取 cmoBuffer，清除 L2 中的原有数据。
bool L2CacheCleaner::WaitClearL2Cache() const {
    if (CheckAclResult(aclrtMemcpyAsyncImplOrigin(flushBuffer_, bufferSize_, buffer_, bufferSize_,
            ACL_MEMCPY_DEVICE_TO_DEVICE, stream_), "aclrtMemcpyAsyncImpl") != ACL_SUCCESS) {
        return false;
    }
    if (CheckAclResult(aclrtSynchronizeStreamImplOrigin(stream_), "aclrtSynchronizeStreamImpl") != ACL_SUCCESS) {
        return false;
    }
    auto chipType = GetProductTypeBySocVersion(DeviceContext::Local().GetSocVersion());
    if (IsChipSeriesTypeValid(chipType, ChipProductType::ASCEND910B_SERIES) ||
        IsChipSeriesTypeValid(chipType, ChipProductType::ASCEND910_93_SERIES)) {
        if (CheckAclResult(aclrtCmoAsyncImplOrigin(cmoBuffer_, bufferSize_, ACL_RT_CMO_TYPE_PREFETCH, stream_),
                "aclrtCmoAsyncImpl") != ACL_SUCCESS) {
            return false;
        }
    }
    return CheckAclResult(aclrtSynchronizeStreamImplOrigin(stream_), "aclrtSynchronizeStreamImpl") == ACL_SUCCESS;
}

void L2CacheCleaner::Reset() {
    std::lock_guard<std::mutex> lock(resetMutex_);
    isReady_ = false;
    FreeBuffer(flushBuffer_, "flush buffer aclrtFreeImpl");
    FreeBuffer(cmoBuffer_, "cmo buffer aclrtFreeImpl");
    FreeBuffer(buffer_, "buffer aclrtFreeImpl");
    FreeBuffer(l2Buffer_, "l2Buffer aclrtFreeImpl");
    FreeBuffer(blockLen_, "blockLen aclrtFreeImpl");
    stream_ = nullptr;
    blockDim_ = 0;
    bufferSize_ = 0;
}

bool L2CacheCleaner::Clear() const {
    if (!isReady_) {
        return false;
    }
    std::vector<void *> inputArgs = {l2Buffer_, blockLen_};
    if (!DFXKernelLauncher::Instance().CallClearL2Cache(blockDim_, stream_, inputArgs)) {
        WARN_LOG("Failed to clear L2cache by operator");
        return false;
    }
    if (!WaitClearL2Cache()) {
        WARN_LOG("Failed to clear L2cache by move memory to L2");
        return false;
    }
    return true;
}

bool L2CacheCleaner::CallEmptyKernel(rtStream_t stream) {
    if (!DFXKernelLauncher::Instance().CallEmptyKernel(stream)) {
        WARN_LOG("Failed to call empty kernel for L2cache");
        return false;
    }
    return true;
}
