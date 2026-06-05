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

#pragma once

#include <elf.h>
#include <functional>
#include <memory>
#include <vector>
#include "runtime/inject_helpers/ProfDataCollect.h"

class RuntimeKernelLaunchMixin {
protected:
    rtDevBinary_t *stubBinPtr_{nullptr};
    void *stubHdl_{nullptr};
    uint32_t blockDim_{0};
    rtSmDesc_t *smDesc_{nullptr};
    uint8_t *memInfo_{nullptr};
    uint64_t memSize_{0};
    std::vector<uint8_t> argsVec_;
    rtStream_t stm_{};
    std::shared_ptr<ProfDataCollect> profObj_;
    bool skipSanitizer_{false};
    uint64_t launchId_{0};
    uint64_t regId_{0};
    std::function<void()> refreshParamFunc_;
    int32_t devId_{0};
    bool isSink_{false};
    std::vector<Elf64_Shdr> sections_;
};
