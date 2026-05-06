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

 #include <algorithm>
#include "DeviceContext.h"
#include "utils/Serialize.h"
#include "utils/Protocol.h"
#include "utils/InjectLogger.h"
#include "core/FuncSelector.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/ConfigManager.h"
#include "MemGuard.h"

bool MemoryGuard::GetGuardBlockInfoByUserAddr(void *userPtr, GuardBlockInfo &blockInfoRst)
{
    std::lock_guard<std::mutex> lock(mutex_);

    int32_t deviceId = DeviceContext::Local().GetDeviceId();
    auto deviceMemMap = guardMemMap_.find(deviceId);
    if (deviceMemMap == guardMemMap_.end()) {
        return false;
    }

    auto blockInfo = deviceMemMap->second.find(userPtr);
    if (blockInfo == deviceMemMap->second.end()) {
        return false;
    }

    blockInfoRst = blockInfo->second;

    return true;
}

size_t MemoryGuard::GetGuardMapSize()
{
    std::lock_guard<std::mutex> lock(mutex_);

    size_t cnt = 0;
    for (auto &deviceMemMapIt : guardMemMap_) {
        cnt += deviceMemMapIt.second.size();
    }

    return cnt;
}

void MemoryGuard::ClearGuardBlockMap()
{
    guardMemMap_.clear();
}

void MemoryGuard::Init()
{
    // 仅当开启内存检测时启用安全区
    SanitizerConfig cliConfig = SanitizerConfigManager::Instance().GetConfig();
    if (!(IsSanitizer() && cliConfig.memCheck)) {
        return;
    }
    memGuardEnable_ = true;

    // 配置安全区大小
    SetGuardSizes(cliConfig.gmBufferGuardSize, cliConfig.gmBufferGuardSize);
    DEBUG_LOG("MemoryGuard get frontSize %zu and backSize %zu from config.", frontSize_, backSize_);

    memGuardInit_ = true;
}

void MemoryGuard::SetGuardSizes(size_t front, size_t back)
{

    if (front == 0 || back == 0) {
        ERROR_LOG("MemoryGuard set guard size invalid, front %zu, back %zu.", front, back);
        return;
    }

    // 前后保护区大小分别对齐到 ALIGNMENT 倍数
    frontSize_ = CeilByAlignSize<ALIGNMENT>(front);
    backSize_ = CeilByAlignSize<ALIGNMENT>(back);

    DEBUG_LOG("MemoryGuard set frontSize %zu and backSize %zu.", frontSize_, backSize_);
}

void MemoryGuard::GetGuardSizes(size_t &front, size_t &back) const
{
    front = frontSize_;
    back = backSize_;
}

void MemoryGuard::SetGuardPattern(unsigned char pattern)
{
    pattern_ = pattern;

    DEBUG_LOG("MemoryGuard set pattern %s.", &pattern_);
}

void MemoryGuard::GetGuardPattern(unsigned char &pattern) const
{
    pattern = pattern_;
}

void MemoryGuard::FillGuard(void* start, size_t len)
{
    if (start && len > 0) {
        aclError error = aclrtMemsetImplOrigin(start, len, static_cast<int32_t>(pattern_), len);
        if (error != ACL_ERROR_NONE) {
            ERROR_LOG("FillGuard error: %d", error);
            return;
        }
    }
}

void MemoryGuard::FillAllMemGuard()
{
    if (!memGuardEnable_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto &deviceMemMapIt : guardMemMap_) {
        for (auto &blockInfoIt : deviceMemMapIt.second) {
            GuardBlockInfo &blockInfo = blockInfoIt.second;
            // 填充前后保护区
            FillGuard(blockInfo.GetFrontStart(), frontSize_);
            FillGuard(blockInfo.GetBackStart(), backSize_);
        }
    }
}

size_t MemoryGuard::CheckGuard(const void* start, size_t len) const
{
    if (len == 0) {
        return 0;
    }

    unsigned char buf[len];
    aclError error = aclrtMemcpyImplOrigin(static_cast<void *>(buf), len, start, len, ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("CheckGuard copy out error: %d", error);
        return 0;
    }

    size_t corrupted_bytes = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != pattern_) {
            ++corrupted_bytes;
        }
    }

    return corrupted_bytes;
}

void MemoryGuard::CheckAllMemGuard()
{
    if (!memGuardEnable_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto &deviceMemMapIt : guardMemMap_) {
        for (auto &blockInfoIt : deviceMemMapIt.second) {
            GuardBlockInfo &blockInfo = blockInfoIt.second;

            // 检查前后保护区
            blockInfo.frontErrBytes = CheckGuard(blockInfo.GetFrontStart(), frontSize_);
            blockInfo.backErrBytes = CheckGuard(blockInfo.GetBackStart(), backSize_);

            // 存在越界写时，生成告警信息并传给工具侧
            if (blockInfo.frontErrBytes + blockInfo.backErrBytes != 0) {
                GenGMAddrErr(blockInfo);
            }
        }
    }
}

void MemoryGuard::GenGMAddrErr(const GuardBlockInfo &blockInfo) const
{
    PacketHead gmHead = { PacketType::GM_ADDR_OUT_OF_BOUND_RECORD };
    GMAddrOutOfBoundRecord gmRecord;
    gmRecord.userAddr = reinterpret_cast<uint64_t>(blockInfo.GetUserAddr());
    gmRecord.size = blockInfo.userSize;
    gmRecord.frontOutSize = blockInfo.frontErrBytes;
    gmRecord.backOutSize = blockInfo.backErrBytes;

    LocalDevice::Local().Notify(Serialize(gmHead, gmRecord));
}

size_t MemoryGuard::GetTotalSize(size_t userSize)
{
    // 首次内存申请时，初始化 MemoryGuard 类
    if (!memGuardInit_) Init();

    if (!memGuardEnable_) return userSize;

    std::lock_guard<std::mutex> lock(mutex_);

    size_t total_size = frontSize_ + userSize + backSize_;

    return total_size;
}

void MemoryGuard::MallocProc(void **devPtr, size_t userSize)
{
    if (!memGuardEnable_) return;

    std::lock_guard<std::mutex> lock(mutex_);
    // 此时内存分配已经成功，直接计算用户地址修改返回指针，并生成记录

    // 记录映射信息
    GuardBlockInfo info;
    info.realPtr = *devPtr;
    info.userSize = userSize;
    info.frontGuardSize = frontSize_;
    info.backGuardSize = backSize_;

    int32_t deviceId = DeviceContext::Local().GetDeviceId();
    guardMemMap_[deviceId][info.GetUserAddr()] = info;

    // 更新返回指针
    *devPtr = info.GetUserAddr();
}

void *MemoryGuard::GetRealPtr(void *userPtr)
{
    if (!memGuardEnable_ || !userPtr) {
        return userPtr;
    }

    GuardBlockInfo blockInfo{};
    if (!GetGuardBlockInfoByUserAddr(userPtr, blockInfo)) {
        return userPtr;
    }

    return blockInfo.realPtr;
}

void MemoryGuard::FreeProc(void *userPtr)
{
    if (!memGuardEnable_ || !userPtr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    int32_t deviceId = DeviceContext::Local().GetDeviceId();
    auto deviceMemMap = guardMemMap_.find(deviceId);
    if (deviceMemMap == guardMemMap_.end()) {
        return;
    }

    auto blockInfo = deviceMemMap->second.find(userPtr);
    if (blockInfo == deviceMemMap->second.end()) {
        return;
    }

    // 从映射表中移除
    deviceMemMap->second.erase(blockInfo);
}
