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

#ifndef __FUNC_CONTEXT_H__
#define __FUNC_CONTEXT_H__

#pragma once

#include <map>
#include <string>

#include "acl.h"
#include "runtime.h"
#include "RegisterContext.h"

/**
 * 管理单个注册的Kernel Function的信息
 * 提供通用的信息查询接口，具体实现取决于Function注册方式, 也适用于rt接口方式的注册
 * kernelName: 需要由子类在构造函数里实现好. 之所有不让子类实现GetKernelName方式是为了性能考虑，
 * 一次性在构造函数实现好，避免重复生成KernelName.
 * funcHandle: 只有aclrt接口注册函数的方式才有funcHanlde
 * 依赖RegisterContext.
 */
class FuncContext {
public:
    using FuncContextSP = std::shared_ptr<FuncContext>;
    FuncContext(RegisterContextSP regCtx, void *funcHandle)
        : funcHandle_(funcHandle), regCtx_(regCtx), kernelName_{} {}

    virtual bool GetTilingKey(uint64_t &tilingKey) const { return false; }

    virtual uint64_t GetKernelPC() const;

    virtual uint64_t GetStartPC() const;

    std::string GetKernelName() const { return kernelName_; }

    RegisterContextSP GetRegisterContext(void) const { return regCtx_; }

    void *GetFuncHandle() const { return funcHandle_; }

    KernelType GetKernelType() const;

    // Create FuncContext and call aclrt api with the same param except register info
    virtual FuncContextSP Clone(const RegisterContextSP &regCtx) const = 0;

    bool isAiCpu = false;

protected:
    KernelType GetKernelTypeFromRuntime() const;

protected:
    void *funcHandle_;
    RegisterContextSP regCtx_;
    std::string kernelName_;
};
using FuncContextSP = std::shared_ptr<FuncContext>;

#endif // __FUNC_CONTEXT_H__