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

#ifndef __LAUNCH_INIT_FINALIZE_H__
#define __LAUNCH_INIT_FINALIZE_H__

#include "utils/Protocol.h"
#include "KernelContext.h"
#include "RegisterContext.h"
#include "LaunchContext.h"

KernelType GetCurrentKernelType(void);
KernelType GetKernelType(KernelContext::RegisterEvent const &registerEvent,
                         KernelContext::LaunchEvent const &launchEvent);

// init协议如下：
// GlobalHead
// blockIdx:0  BlockHead | HostMalloc | SimdRecord | SimtRecord | ShadowMemory | Align
// blockIdx:1  BlockHead | HostMalloc | SimdRecord | SimtRecord | ShadowMemory | Align
// ......
class SanitizerLaunchInit {
public:
    SanitizerLaunchInit() = default;
    ~SanitizerLaunchInit() = default;
    void Init(uint64_t blockDim);
    uint8_t* Process();

private:
    void InitKernelType();
    bool AssignGlobalHead();
    void AssignSimtVfInfo();
    bool BlockHeadsH2D(uint8_t *memInfo) const;
    bool HostMallocH2D(uint8_t *memInfo, size_t blockIdx) const;
    bool SimtBlockHeadsH2D(uint8_t *memInfo, size_t blockIdx) const;
    uint8_t* SkipKernelProcess() const;

    uint64_t blockDim_{};
    uint32_t hostMemoryNum_{};
    RecordGlobalHead globalHead_{};
    KernelType kernelType_{KernelType::INVALID};
};


// finalize协议如下：
// GlobalHead
// blockIdx:0  BlockHead | SimdRecord | SimtRecord | ShadowMemory | Align
// blockIdx:1  BlockHead | SimdRecord | SimtRecord | ShadowMemory | Align
// ......
class SanitizerLaunchFinalize {
public:
    SanitizerLaunchFinalize() = default;
    ~SanitizerLaunchFinalize();
    void Init(const uint8_t *memInfo, uint64_t blockDim);
    void Process();

private:
    void UpdateOffsetStatus(uint64_t offset, uint64_t d2HOffsetDiff = 0);
    void ResetOffsetStatus(size_t blockIdx);
    bool GlobalHeadD2H();
    bool RegistersD2H() const;
    bool BlockHeadD2H(size_t blockIdx);
    bool SimdRecordD2H();
    bool SimtErrorD2H();
    bool ShadowMemoryD2H();
    bool SimtEntryD2H();
    void ReportBlockInfo();

    uint64_t blockDim_{};
    uint64_t blockSize_{};                // host侧需要的blockSize
    uint64_t blockRemainSize_{};          // host侧拷贝后blockSize还剩余多少size
    uint64_t sendMemorySize_{};
    const uint8_t *memInfo_{nullptr};
    const uint8_t *memInfoBackUp_{nullptr};
    uint8_t *memInfoHost_{nullptr};
    uint8_t *memInfoHostBackUp_{nullptr};
    RecordGlobalHead *globalHead_{nullptr};
    RecordBlockHead *blockHead_{nullptr};
};

#endif // __LAUNCH_INIT_FINALIZE_H__
