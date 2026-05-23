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

#include <mutex>
#include <unordered_map>
#include "utils/Protocol.h"
#include "utils/Numeric.h"
#include "utils/Singleton.h"
#include "acl.h"

#pragma once

// 实际内存块信息
struct GuardBlockInfo {
    void *realPtr;          // 实际分配的首地址
    size_t userSize;        // 用户请求/可见的大小

    // 前后保护区大小
    size_t frontGuardSize;
    size_t backGuardSize;

    // 前后保护区异常越界起始位置
    size_t frontErrStart = -1;
    size_t backErrStart = -1;

    // 前后保护区异常越界写字节数
    size_t frontErrBytes;
    size_t backErrBytes;

    void *GetFrontStart() const { return realPtr; }
    void *GetUserAddr()   const { return static_cast<char *>(realPtr) + frontGuardSize; }
    void *GetBackStart()  const { return static_cast<char *>(realPtr) + frontGuardSize + userSize; }
};

/**
 * @brief 内存越界检测守护单例类
 * 
 * 在劫持的 malloc/free 中记录用户可见内存地址与实际内存地址大小关系，并检测越界写入
 */
class MemoryGuard : public Singleton<MemoryGuard, false> {
public:
    friend class Singleton<MemoryGuard, false>;

    void Init();
    /**
     * @brief 设置前后保护区大小
     * @param frontSize 前保护区字节数
     * @param backSize  后保护区字节数
     */
    void SetGuardSizes(size_t frontSize, size_t backSize);
    void GetGuardSizes(size_t &frontSize, size_t &backSize) const;

    /**
     * @brief 设置保护区填充字节模式（默认 0xAA）
     */
    void SetGuardPattern(unsigned char pattern);
    void GetGuardPattern(unsigned char &pattern) const;

    size_t GetTotalSize(size_t userSize);
    void MallocProc(void **devPtr, size_t userSize);

    void *GetRealPtr(void *userPtr);
    void FreeProc(void *userPtr);

    size_t GetGuardMapSize();
    bool GetGuardBlockInfoByUserAddr(void *userPtr, GuardBlockInfo &blockInfo);
    void ClearGuardBlockMap();

    /**
     * @brief 填充所有保护区
     */
    void FillAllMemGuard();

    /**
     * @brief 检查所有保护区是否有被篡改
     */
    void CheckAllMemGuard();

private:

    // 按32位向上对齐
    static constexpr size_t ALIGNMENT = 32;

    // 填充单个保护区
    void FillGuard(void* start, size_t len);

    // 检查保护区是否完整
    void CheckGuard(const void* start, size_t len, uint64_t &errBytesStart, size_t &errBytes) const;

    // 生成GM内存越界记录并发送给工具侧
    void GenGMAddrErr(const GuardBlockInfo &blockInfo) const;

public:
    bool memGuardInit_ = false;
    bool memGuardEnable_ = false; // 内存安全区全局功能使能开关

private:
    size_t frontSize_ = 0;  // 默认不使用前安全区
    size_t backSize_ = GM_BUFFER_GUARD_DFT_SIZE;
    unsigned char pattern_ = GM_BUFFER_GUARD_DFT_PATTERN;

    std::mutex mutex_;
    std::unordered_map<int32_t, std::unordered_map<void*, GuardBlockInfo>> guardMemMap_;   // key1: deviceId, key2: userAddr
};
