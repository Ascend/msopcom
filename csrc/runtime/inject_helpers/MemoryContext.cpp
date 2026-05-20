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

#include "utils/InjectLogger.h"

void MemoryContext::Append(const MemAddr addr, uint64_t size)
{
    std::lock_guard<std::mutex> mapLock(mapMtx_);
    if (addr == nullptr) {
        WARN_LOG("Failed to add memory map, devPtr is nullptr.");
        return;
    }
    memSectionMap_[addr] = {addr, nullptr, size, aclrtMemMallocPolicy::ACL_MEM_MALLOC_HUGE_FIRST};
}

void MemoryContext::Discard(MemAddr addr)
{
    // record memory free in aclrtFreeImpl
    std::lock_guard<std::mutex> mapLock(mapMtx_);
    auto it = memSectionMap_.find(addr);
    if (it == memSectionMap_.end()) {
        return;
    }

    if (it->second.snapshotAddr != nullptr) {
        (void)aclrtFreeImplOrigin(it->second.snapshotAddr);
    }
    memSectionMap_.erase(addr);
}

bool MemoryContext::Backup()
{
    // 1. 优先获取当前stream，使用异步拷贝+流同步进行备份
    // 2. 若获取stream失败，则使用同步拷贝进行备份
    std::lock_guard<std::mutex> mapLock(mapMtx_);
    for (auto &p : memSectionMap_) {
        auto ret = CreateSnapshot(p.second, GetCurrentStream());
        if (!ret) {
            return false;
        }
    }
    return true;
}

bool MemoryContext::Restore()
{
    std::lock_guard<std::mutex> mapLock(mapMtx_);
    // 1. 优先获取当前stream，使用异步拷贝+流同步进行备份
    // 2. 若获取stream失败，则使用同步拷贝进行备份
    for (const auto &p : memSectionMap_) {
        auto ret = RestoreFromSnapshot(p.second, GetCurrentStream());
        if (!ret) {
            return false;
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
    for (const auto &p : memSectionMap_) {
        if (p.second.snapshotAddr != nullptr) {
            (void)aclrtFreeImplOrigin(p.second.snapshotAddr);
        }
    }
    std::lock_guard<std::mutex> mapLock(mapMtx_);
    memSectionMap_.clear();
    stream_ = nullptr;
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
        ERROR_LOG("there is no snapshot memory allocated for 0x%lx",
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

aclrtStream MemoryContext::GetCurrentStream()
{
    std::lock_guard<std::mutex> lock(streamMtx_);
    if (stream_ != nullptr) {
        return stream_;
    }

    aclError ret = aclrtCtxGetCurrentDefaultStreamImplOrigin(&stream_);
    if (ret != ACL_SUCCESS) {
        stream_ = nullptr;
        DEBUG_LOG("aclrtCtxGetCurrentDefaultStreamImplOrigin failed. ret=%d", ret);
    }
    return stream_;
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
