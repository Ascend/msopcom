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

#include <functional>
#include <memory>
#include "runtime/inject_helpers/ArgsContext.h"
#include "runtime/inject_helpers/FuncContext.h"
#include "runtime/inject_helpers/LaunchContext.h"
#include "runtime/inject_helpers/ProfDataCollect.h"

class AclLaunchKernelMixin {
protected:
    aclrtFuncHandle funcHandle_{nullptr};
    aclrtStream stream_{nullptr};
    std::shared_ptr<ProfDataCollect> profObj_{nullptr};
    std::function<void()> refreshParamFunc_{nullptr};
    int32_t devId_{0};
    uint8_t *memInfo_{nullptr};
    uint64_t memSize_{0};
    FuncContextSP funcCtx_{nullptr};
    LaunchContextSP launchCtx_{nullptr};
    ArgsContextSP newArgsCtx_{nullptr};
    uint64_t regId_{0};
    bool skipSanitizer_{false};
    bool isSink_{false};
};
