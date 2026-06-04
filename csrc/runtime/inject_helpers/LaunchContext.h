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

#ifndef __LAUNCH_CONTEXT_H__
#define __LAUNCH_CONTEXT_H__

#pragma once

#include <map>
#include <string>
#include <memory>

#include "acl.h"
#include "runtime.h"
#include "FuncContext.h"
#include "ArgsContext.h"
#include "KernelReplacement.h"

bool DumpConfig(const std::string &outputDir, const DumperContext &config);

struct LaunchParam {
    uint32_t blockDim;
    aclrtStream stream;
    bool isSink;
    uint64_t launchId;
    aclrtLaunchKernelCfg *cfg;
};

class LaunchContext {
public:
    LaunchContext(const FuncContextSP &funcCtx, const ArgsContextSP &argsCtx, const LaunchParam &param) : funcCtx_(funcCtx), argsCtx_(argsCtx), param_(param), dbiFuncCtx_{} {}

    bool Save(const std::string &outputPath) const;

    FuncContextSP GetFuncContext() const { return funcCtx_; }

    ArgsContextSP GetArgsContext() const { return argsCtx_; }

    uint64_t GetLaunchId() const { return param_.launchId; }

    LaunchParam const &GetLaunchParam() const { return param_; }

    bool IsSink() const { return param_.isSink; }

    void SetDBIFuncCtx(const FuncContextSP dbiFuncCtx) { dbiFuncCtx_ = dbiFuncCtx; }

    FuncContextSP GetDBIFuncCtx() const;

    void UpdateOpMemInfoByAdump(OpMemInfo &memInfo) const;

private:
    FuncContextSP funcCtx_;
    ArgsContextSP argsCtx_;
    LaunchParam param_;
    // 这里并不想影响着动态插桩数据的生命周期
    std::weak_ptr<FuncContext> dbiFuncCtx_;
};

using LaunchContextSP = std::shared_ptr<LaunchContext>;

#endif // __LAUNCH_CONTEXT_H__
