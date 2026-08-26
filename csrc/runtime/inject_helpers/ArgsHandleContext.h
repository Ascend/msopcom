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

#pragma once

#include <map>
#include <string>
#include <vector>
#include <memory>

#include "acl.h"
#include "runtime.h"
#include "runtime/inject_helpers/KernelMetaDataParser.h"
#include "ArgsContext.h"

/*
 * 管理aclrtKernelArgsInit方式创建的Args上下文
 */
class ArgsHandleContext : public ArgsContext {
public:
    enum class ParamType : uint8_t {
        NORMAL = 0,  // normal parameter
        PLACEHOLDER  // placeholder
    };

    struct ParamData {
        aclrtParamHandle paramHandle{nullptr};
        std::vector<uint8_t> param;
        ParamType type{ParamType::NORMAL};
        void *srcBuffer{nullptr};
    };

public:
    ArgsHandleContext(aclrtFuncHandle funcHandle, aclrtArgsHandle argsHandle)
        : funcHandle_(funcHandle), argsHandle_(argsHandle) {}
    void Swap(ArgsHandleContext &rhs);

    void SetFuncHandle(aclrtFuncHandle funcHandle) { funcHandle_ = funcHandle; }

    bool ExpandArgs(void *param, size_t paramSize, uint32_t &paramOffset) override;

    bool Save(const std::string &outputPath, DumperContext &config, OpMemInfo &memInfo, bool isSink) override;

    ArgsContextSP Clone(void) const override;

    bool GetTilingData(std::vector<uint8_t> &data) const override;

    // 记录每次做aclrtKernelArgsAppend的操作
    void CacheArgsAppendOp(void *param, size_t paramSize, aclrtParamHandle paramHandle);
    // 记录每次做aclrtKernelArgsAppendPlaceHolder的操作
    void CacheArgsAppendPlaceholderOp(aclrtParamHandle paramHandle);

    // 记录每次做aclrtKernelArgsGetPlaceHolderBuffer的操作
    void CacheArgsGetPlaceholderBufferOp(aclrtParamHandle paramHandle, size_t dataSize, void *buffer);

    // 记录每次做aclrtKernelArgsParaUpdate的操作
    void CacheArgsParaUpdateOp(aclrtParamHandle paramHandle, void *param, size_t paramSize);

    // 记录每次做aclrtKernelArgsFinalize的操作
    void Finalize();

    bool IsFinalized() const { return finalized_; }

    size_t CalcUserArgsSize(void) const;
    // 重放记录生成新的aclrtArgsHandle
    aclrtArgsHandle GenerateArgsHandle();

#if defined (__BUILD_TESTS__)
    std::vector<ParamData> GetParams() { return params_; }
#endif

private:
    bool ReplayArgs(aclrtArgsHandle argsHandle);
    bool UpdateNormalTaskArgsAddr(OpMemInfo &memInfo) const;

private:
    std::vector<ParamData> params_;
    aclrtFuncHandle funcHandle_;
    aclrtArgsHandle argsHandle_;
    bool finalized_{false};
};

using ArgsHandleContextSP = std::shared_ptr<ArgsHandleContext>;
