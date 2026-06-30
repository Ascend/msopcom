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

#ifndef KERNEL_LAUNCHER_KERNEL_RUNNER_H
#define KERNEL_LAUNCHER_KERNEL_RUNNER_H

#include <string>
#include <vector>

#include "KernelConfigParser.h"
#include "RuntimeApi.h"

class KernelRunner {
    using Param = KernelConfig::Param;
public:
    bool Run(const KernelConfig& kernelConfig);

    virtual ~KernelRunner();

private:
    bool RegisterKernel(const KernelConfig& kernelConfig, const std::vector<char> &data, uint64_t fileSize);

    bool LaunchKernel(KernelConfig const &kernelConfig);

    bool InitInput(const Param &in);

    bool InitOutput(const Param &out);

    bool InitWorkspace(const Param &workspace);

    bool InitTiling(const Param &tiling);

    bool InitFftsAddr(const Param &fftsAddr);

    bool InitFftsAddr();

    bool SaveOutputs(const std::string &outputDir);

    bool InitData(const KernelConfig &kernelConfig);

    RuntimeApi rtAPI_{};
    rtStream_t rtStream_;
    int deviceID_ = 0;
    void *binHandle_ = nullptr;
    bool needDestroyStream_ = false;
    bool needResetDevice_ = false;
    bool needUnRegisterDevBinary_ = false;
    std::vector<void *> kernelArgs_;
    std::vector<void *> hostInputPtrs_;
    std::vector<void *> devInputPtrs_;
    std::vector<void *> hostOutputPtrs_;
    std::vector<void *> devOutputPtrs_;
    std::vector<Param> outputs_;
    std::string caseName_;
    std::string registerStub_;
    std::size_t tilingAddrOffset_ {};
    std::size_t tilingDataSize_ {};
    void *tilingData_ {};
};
#endif // KERNEL_LAUNCHER_KERNEL_RUNNER_H
