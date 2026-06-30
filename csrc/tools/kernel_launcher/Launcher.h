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

#ifndef KERNEL_LAUNCHER_H
#define KERNEL_LAUNCHER_H

#include <string>
#include <vector>

#include "KernelConfigParser.h"
#include "utils/InjectLogger.h"
#include "acl.h"

class Launcher {
    using Param = KernelConfig::Param;
public:
    bool Run(const KernelConfig& kernelConfig);

    virtual ~Launcher();

private:
    bool RegisterKernel(const KernelConfig& kernelConfig, std::string const &filename);

    bool LaunchKernel(KernelConfig const &kernelConfig);

    bool InitInput(const Param &in);

    bool InitOutput(const Param &out);

    bool InitWorkspace(const Param &workspace);

    bool InitTiling(const Param &tiling);

    bool SaveOutputs(const std::string &outputDir);

    bool InitData(const KernelConfig& kernelConfig);

    void GenJson(const KernelConfig& kernelConfig);

    int32_t deviceID_ {0};
    aclrtStream stream_;
    aclrtBinHandle binHandle_;
    aclrtFuncHandle funcHandle_;
    aclrtArgsHandle argsHandle_;
    bool needDestroyStream_ = false;
    bool needResetDevice_ = false;
    bool needUnRegisterDevBinary_ = false;
    bool needDeleteJson_ = false;
    std::vector<void *> kernelArgs_;
    std::vector<void *> hostInputPtrs_;
    std::vector<void *> devInputPtrs_;
    std::vector<void *> hostOutputPtrs_;
    std::vector<void *> devOutputPtrs_;
    std::vector<Param> outputs_;
    std::string caseName_;
    std::string registerStub_;
    std::string jsonFilePath_;
};
#endif // KERNEL_LAUNCHER_H
