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

#ifndef __RUNTIME_INJECT_HELPERS_KERNEL_REPLACEMENT_H__
#define __RUNTIME_INJECT_HELPERS_KERNEL_REPLACEMENT_H__

#include <string>
#include <vector>
#include <memory>

#include "runtime.h"
#include "core/FunctionLoader.h"
#include "runtime/inject_helpers/KernelMatcher.h"
#include "runtime/inject_helpers/FuncContext.h"
#include "core/BinaryInstrumentation.h"
#include "utils/Future.h"

class KernelReplaceTask {
public:
    explicit KernelReplaceTask(const std::string &kernelPath) : kernelPath_(kernelPath) {}

    /**
     * 执行指定 regId 对应的算子二进制替换
     * @param handle 替换完成后的二进制句柄
     * @param regId 要替换的 kernel 对应的注册事件 ID
     * @param withStubFunc 二进制后续是否需要执行函数注册，二进制注册时会根据此变量调用
     *        rtDevBinaryRegister 或 rtRegisterAllKernel 完成新的 kernel 注册
     */
    bool Run(void **handle, uint64_t regId, bool withStubFunc);
    void *GetHandle();
    RegisterContextSP GetRegisterContext() const { return regCtx_; }
    ~KernelReplaceTask();
    rtDevBinary_t const &GetDevBinary(void) const { return bin_; }

private:
    rtDevBinary_t bin_{};
    std::vector<char> binaryData_;
    void *handle_{nullptr};
    aclrtBinHandle binHandle_{nullptr};
    RegisterContextSP regCtx_{};

    std::string kernelPath_;
};

using KernelReplaceTaskSP = std::shared_ptr<KernelReplaceTask>;

/*
 * KernelReplacement is used to replace kernel binary
 * which matched by the target launch id.
 * We will register new kernel binary,
 * then user can use the handle to launch new kernel function.
 * If the old kernel has multiple kernel function or multiple launch,
 * we only register new kernel once for the first time matched.
 * The new kernel data will be unregistered until the old kernel binary unregister
 *
 * Limit:
 * 1. The replacement is only used on kernel launch.
 * 2. both old and new kernel binary must have the same kernel function set, and the same chip type
 * 3. only support replace one new kernel binary.
 *
 * future:
 * 1. support multiple kernel binary
 * 2. maybe replace kernel at the time of registerKernel instead of kernelLaunch
 *
 */

class KernelReplacement {
public:
    static KernelReplacement &Instance()
    {
        static KernelReplacement inst;
        return inst;
    }

    void Init(const std::string &kernelPath, const KernelMatcher::Config &config)
    {
        kernelPath_ = kernelPath;
        matcher_ = MakeShared<KernelMatcher>(config);
    }

    bool CreateHandle(void **handle, uint64_t launchId);

    bool ReleaseHandle(const void *handle);

private:
    KernelReplacement() = default;

private:
    uint64_t oldKernelRegId_{UINT64_MAX};

    std::string kernelPath_;
    std::shared_ptr<KernelMatcher> matcher_;
    KernelReplaceTaskSP replaceTask_{};
};

class KernelDumper {
public:
    static KernelDumper &Instance()
    {
        static KernelDumper inst;
        return inst;
    }

    static void DumpDataCallback(void *fnData);

    void Init(const std::string &outputDir, const KernelMatcher::Config &config)
    {
        outputDir_ = outputDir;
        matcher_ = MakeShared<KernelMatcher>(config);
    }

    bool Dump(uint64_t launchId, bool aclNew = false) const;

    // 直接dump此次拉起的kernel，不进行匹配
    bool Dump(const std::string &outputDir, uint64_t launchId = UINT64_MAX, bool aclNew = false) const;

    bool DumpAicore(const std::string &outputDir) const; // MC2引入，msopt落盘aicore.o

    bool LaunchDumpTask(rtStream_t stm, bool aclNew = false);

private:
    uint64_t dumpNum_{0};
    std::string outputDir_;
    std::shared_ptr<KernelMatcher> matcher_;
};

#endif // __RUNTIME_INJECT_HELPERS_KERNEL_REPLACEMENT_H__
