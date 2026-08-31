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

#include "FuncContext.h"

#include <cinttypes>

#include "utils/InjectLogger.h"
#include "acl_rt_impl/AscendclImplOrigin.h"

using namespace std;

namespace {
KernelType AclrtKernelTypeTrans(aclrtKernelType kernelType)
{
    switch (kernelType) {
        case aclrtKernelType::ACL_KERNEL_TYPE_AICORE:
        case aclrtKernelType::ACL_KERNEL_TYPE_MIX:
            return KernelType::MIX;
        case aclrtKernelType::ACL_KERNEL_TYPE_VECTOR:
            return KernelType::AIVEC;
        case aclrtKernelType::ACL_KERNEL_TYPE_CUBE:
            return KernelType::AICUBE;
        case aclrtKernelType::ACL_KERNEL_TYPE_AICPU:
            return KernelType::AICPU;
        default:
            return KernelType::AIVEC;
    }
}
} // namespace

uint64_t FuncContext::GetKernelPC() const
{
    if (isAiCpu) {
        return 0;
    }

    void *aicAddr{nullptr};
    void *aivAddr{nullptr};
    uint64_t pcKernelAddr;
    auto ret = aclrtGetFunctionAddrImplOrigin(funcHandle_, &aicAddr, &aivAddr);
    if (ret != 0) {
        WARN_LOG("aclrtGetFunctionAddrImpl failed. ret=%d\n", ret);
        return 0;
    }
    if (aicAddr) {
        pcKernelAddr = reinterpret_cast<uint64_t>(aicAddr);
    } else if (aivAddr) {
        pcKernelAddr = reinterpret_cast<uint64_t>(aivAddr);
    } else {
        ERROR_LOG("aclrtGetFunctionAddrImpl get addr all zero");
        return 0;
    }
    return pcKernelAddr;
}

uint64_t FuncContext::GetStartPC() const
{
    auto pcStartAddr = GetKernelPC();
    if (pcStartAddr == 0) {
        return pcStartAddr;
    }
    uint64_t kernelOffset = regCtx_->GetKernelOffsetByName(kernelName_);
    if (pcStartAddr < kernelOffset) {
        ERROR_LOG("pc start addr %#" PRIx64 " smaller than kernel offset %#" PRIx64, pcStartAddr, kernelOffset);
        return 0;
    }
    pcStartAddr -= kernelOffset;
    return pcStartAddr;
}

KernelType FuncContext::GetKernelType() const
{
    auto kernelType = MagicToKernelType(regCtx_->GetMagic());
    if (kernelType != KernelType::MIX) {
        return kernelType;
    }

    kernelType = GetKernelTypeFromRuntime();
    if (kernelType == KernelType::AICPU) {
        DEBUG_LOG("kernel name: %s is aicpu function", kernelName_.c_str());
    } else if (kernelType == KernelType::INVALID) {
        WARN_LOG("kernel name: %s is get kernel type failed, use default CUBE type", kernelName_.c_str());
        kernelType = KernelType::AICUBE;
    }
    return kernelType;
}

KernelType FuncContext::GetKernelTypeFromRuntime() const
{
    int64_t value;
    auto ret = aclrtGetFunctionAttributeImplOrigin(funcHandle_, aclrtFuncAttribute::ACL_FUNC_ATTR_KERNEL_TYPE, &value);
    if (ret != ACL_SUCCESS) {
        ERROR_LOG("get function kernel type failed, ret=%u", static_cast<uint32_t>(ret));
        return KernelType::INVALID;
    }
    KernelType kernelType = AclrtKernelTypeTrans(static_cast<aclrtKernelType>(value));
    DEBUG_LOG("get kernel type from aclrtGetFunctionAttribute: %u", static_cast<uint32_t>(kernelType));
    return kernelType;
}

bool FuncContext::QueryParamInfo()
{
    size_t paramCount = 0;
    if (funcHandle_ == nullptr ||
        aclrtFunctionGetParamCountImplOrigin(funcHandle_, &paramCount) != ACL_SUCCESS || paramCount == 0) {
        WARN_LOG("Get kernel param count failed or kernel has no param.");
        return false;
    }
    paramCount_ = paramCount;
    paramOffsets_.clear();
    paramSizes_.clear();
    for (size_t i = 0; i < paramCount; ++i) {
        size_t offset = 0;
        size_t size = 0;
        if (aclrtFunctionGetParamInfoImplOrigin(funcHandle_, i, &offset, &size) != ACL_SUCCESS) {
            WARN_LOG("Get kernel param info failed, index: %lu.", i);
            return false;
        }
        paramOffsets_.push_back(offset);
        paramSizes_.push_back(size);
    }
    return true;
}
