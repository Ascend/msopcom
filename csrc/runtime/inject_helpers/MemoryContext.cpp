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

#include "MemoryContext.h"

#include <string>

#include "runtime/inject_helpers/DeviceContext.h"
#include "utils/InjectLogger.h"
MemoryContext::DeviceBucket &MemoryContext::GetBucket() {
    int32_t devId = DeviceContext::GetRunningDeviceId();
    std::lock_guard<std::mutex> lock(bucketsMtx_);
    auto it = buckets_.find(devId);
    if (it == buckets_.end()) {
        auto p = std::unique_ptr<DeviceBucket>(new DeviceBucket());
        auto *ptr = p.get();
        buckets_[devId] = std::move(p);
        return *ptr;
    }
    return *it->second;
}

void MemoryContext::Append(const MemAddr addr, uint64_t size)
{
    auto &bucket = GetBucket();
    std::lock_guard<std::mutex> mapLock(bucket.mapMtx);
    if (addr == nullptr) {
        WARN_LOG("Failed to add memory map, devPtr is nullptr.");
        return;
    }
    bucket.memSectionMap[addr] = {addr, nullptr, size, aclrtMemMallocPolicy::ACL_MEM_MALLOC_HUGE_FIRST};
}

void MemoryContext::Discard(MemAddr addr)
{
    // record memory free in aclrtFreeImpl
    auto &bucket = GetBucket();
    std::lock_guard<std::mutex> mapLock(bucket.mapMtx);
    auto it = bucket.memSectionMap.find(addr);
    if (it == bucket.memSectionMap.end()) {
        return;
    }

    if (it->second.snapshotAddr != nullptr) {
        (void)aclrtFreeImplOrigin(it->second.snapshotAddr);
    }
    bucket.memSectionMap.erase(addr);
}

bool MemoryContext::Backup()
{
    // 1. 优先获取当前stream，使用异步拷贝+流同步进行备份
    // 2. 若获取stream失败，则使用同步拷贝进行备份
    auto &bucket = GetBucket();
    std::lock_guard<std::mutex> mapLock(bucket.mapMtx);
    for (auto &p : bucket.memSectionMap) {
        auto ret = CreateSnapshot(p.second, GetCurrentStream(bucket));
        if (!ret) {
            return false;
        }
    }
    return true;
}

bool MemoryContext::Restore()
{
    auto &bucket = GetBucket();
    std::lock_guard<std::mutex> mapLock(bucket.mapMtx);
    // 1. 优先获取当前stream，使用异步拷贝+流同步进行备份
    // 2. 若获取stream失败，则使用同步拷贝进行备份
    for (const auto &p : bucket.memSectionMap) {
        auto ret = RestoreFromSnapshot(p.second, GetCurrentStream(bucket));
        if (!ret) {
            DEBUG_LOG("Restore memory failed for 0x%lx, continue restoring remaining regions",
                reinterpret_cast<uint64_t>(p.second.originAddr));
        }
    }
    return true;
}

MemoryContext::~MemoryContext()
{
    DiscardAll();
}

void MemoryContext::DiscardAll()
{
    // 释放所有 device 桶下记录的快照内存
    std::lock_guard<std::mutex> bucketsLock(bucketsMtx_);
    for (auto &p : buckets_) {
        auto &bucket = *p.second;
        for (const auto &m : bucket.memSectionMap) {
            if (m.second.snapshotAddr != nullptr) {
                (void)aclrtFreeImplOrigin(m.second.snapshotAddr);
            }
        }
        std::lock_guard<std::mutex> mapLock(bucket.mapMtx);
        bucket.memSectionMap.clear();
        bucket.stream = nullptr;
    }
    buckets_.clear();
}

bool MemoryContext::CreateSnapshot(MemSection &section, aclrtStream stream) const
{
    aclError ret;
    if (section.originAddr == nullptr) {
        ERROR_LOG("failed to create snapshot for an invalid addr");
        return false;
    }

    if (section.snapshotAddr == nullptr) {
        ret = aclrtMallocImplOrigin(&section.snapshotAddr, section.size, section.policy);
        if (ret != ACL_SUCCESS) {
            ERROR_LOG("failed to create memory snapshot for 0x%lx ret=%d",
                reinterpret_cast<uint64_t>(section.originAddr), ret);
            return false;
        }
    }

    MemCopyDirection dir = MemCopyDirection::ORIGIN_TO_SNAPSHOT;

    // 未指定stream，进行同步拷贝
    if (stream == nullptr) {
        return MemCopySync(section, dir);
    }

    // 指定stream，进行异步拷贝+流同步
    return MemCopyWithStream(section, stream, dir);
}

bool MemoryContext::RestoreFromSnapshot(const MemSection &section, aclrtStream stream) const
{
    if (section.originAddr == nullptr) {
        ERROR_LOG("failed to restore an invalid addr");
        return false;
    }

    if (section.snapshotAddr == nullptr) {
        DEBUG_LOG("there is no snapshot memory allocated for 0x%lx, skip restoring",
            reinterpret_cast<uint64_t>(section.originAddr));
        return false;
    }

    MemCopyDirection dir = MemCopyDirection::SNAPSHOT_TO_ORIGIN;

    // 未指定stream，进行同步拷贝
    if (stream == nullptr) {
        return MemCopySync(section, dir);
    }

    // 指定stream，进行异步拷贝+流同步
    return MemCopyWithStream(section, stream, dir);
}

aclrtStream MemoryContext::GetCurrentStream(DeviceBucket &bucket) {
    std::lock_guard<std::mutex> lock(bucket.streamMtx);
    if (bucket.stream != nullptr) {
        return bucket.stream;
    }

    aclError ret = aclrtCtxGetCurrentDefaultStreamImplOrigin(&bucket.stream);
    if (ret != ACL_SUCCESS) {
        bucket.stream = nullptr;
        DEBUG_LOG("aclrtCtxGetCurrentDefaultStreamImplOrigin failed. ret=%d", ret);
    }
    return bucket.stream;
}

bool MemoryContext::MemCopyWithStream(const MemSection &section, aclrtStream stream, MemCopyDirection dir) const
{
    // 指定了stream，进行异步拷贝+流同步
    aclError ret;
    MemAddr dst;
    MemAddr src;
    if (dir == MemCopyDirection::ORIGIN_TO_SNAPSHOT) {
        dst = section.snapshotAddr;
        src = section.originAddr;
    } else {
        dst = section.originAddr;
        src = section.snapshotAddr;
    }

    ret = aclrtMemcpyAsyncImplOrigin(dst, section.size, src, section.size, ACL_MEMCPY_DEVICE_TO_DEVICE, stream);
    if (ret != ACL_SUCCESS) {
        ERROR_LOG("aclrtMemcpyAsyncImpl copy return failed. ret=%d, dst_addr=%p, dst_size=%lu, "
                  "src_addr=%p, src_size=%lu", ret, dst, section.size, src, section.size);
        return false;
    }

    ret = aclrtSynchronizeStreamImplOrigin(stream);
    if (ret != ACL_SUCCESS) {
        ERROR_LOG("aclrtSynchronizeStreamImpl failed. ret=%d", ret);
        return false;
    }
    return true;
}

bool MemoryContext::MemCopySync(const MemSection &section, MemCopyDirection dir) const
{
    MemAddr dst;
    MemAddr src;
    if (dir == MemCopyDirection::ORIGIN_TO_SNAPSHOT) {
        dst = section.snapshotAddr;
        src = section.originAddr;
    } else {
        dst = section.originAddr;
        src = section.snapshotAddr;
    }
    aclError ret = aclrtMemcpyImplOrigin(dst, section.size, src, section.size, ACL_MEMCPY_DEVICE_TO_DEVICE);
    if (ret != ACL_SUCCESS) {
        ERROR_LOG("aclrtMemcpyImpl copy return failed. ret=%d, dst_addr=%p, dst_size=%lu, "
                  "src_addr=%p, src_size=%lu", ret, section.snapshotAddr,
                  section.size, section.originAddr, section.size);
        return false;
    }
    return true;
}
