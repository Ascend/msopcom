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

#ifndef MSOPPROF_RUNTIME_INJECT_HELPERS_L2_CACHE_CLEANER_H
#define MSOPPROF_RUNTIME_INJECT_HELPERS_L2_CACHE_CLEANER_H

#include <cstdint>
#include <mutex>
#include "runtime.h"

// 一次重放过程独占清 L2 所需的缓冲区，准备失败或离开作用域时自动回收。
// 仅串行化 Reset；Prepare/Clear/IsReady 需由调用方串行调用，不能与 Reset 并发。
// 析构前需结束其他访问。
class L2CacheCleaner {
public:
    L2CacheCleaner() = default;
    ~L2CacheCleaner();
    L2CacheCleaner(const L2CacheCleaner &) = delete;
    L2CacheCleaner &operator=(const L2CacheCleaner &) = delete;

    bool Prepare(rtStream_t stream, int32_t deviceId);
    bool Clear() const;
    void Reset();
    bool IsReady() const { return isReady_; }
    static bool CallEmptyKernel(rtStream_t stream);

private:
    bool MallocBuffer();
    bool WaitClearL2Cache() const;

    std::mutex resetMutex_;
    void *flushBuffer_{nullptr};
    void *buffer_{nullptr};
    void *cmoBuffer_{nullptr};
    void *blockLen_{nullptr};
    void *l2Buffer_{nullptr};
    rtStream_t stream_{nullptr};
    uint32_t blockDim_{0};
    uint64_t bufferSize_{0};
    bool isReady_{false};
};

#endif // MSOPPROF_RUNTIME_INJECT_HELPERS_L2_CACHE_CLEANER_H
