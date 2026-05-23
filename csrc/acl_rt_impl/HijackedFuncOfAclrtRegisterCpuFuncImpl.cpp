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


#include "HijackedFunc.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/FuncManager.h"
#include "utils/InjectLogger.h"

HijackedFuncOfAclrtRegisterCpuFuncImpl::HijackedFuncOfAclrtRegisterCpuFuncImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtRegisterCpuFuncImpl") {}

void HijackedFuncOfAclrtRegisterCpuFuncImpl::Pre(
    const aclrtBinHandle binHandle, const char *funcName, const char *kernelName, aclrtFuncHandle *funcHandle
)
{
    binHandle_ = const_cast<const aclrtBinHandle>(binHandle);
    funcName_ = funcName;
    kernelName_ = kernelName;
    funcHandle_ = funcHandle;
}

aclError HijackedFuncOfAclrtRegisterCpuFuncImpl::Post(aclError ret)
{
    if (ret != ACL_SUCCESS || funcName_ == nullptr || kernelName_ == nullptr ||
        binHandle_ == nullptr || funcHandle_ == nullptr ||
        *funcHandle_ == nullptr) {
        return ret;
    }
    uint64_t length = GetValidLength(funcName_, KERNEL_NAME_MAX);
    std::string subName(funcName_, length);
    auto ctx = FuncManager::Instance().CreateContext(binHandle_, subName.c_str(), *funcHandle_);
    if (!ctx) {
        DEBUG_LOG("Create func context failed for kernelName=%s", subName.c_str());
    }
    ctx->isAiCpu = true;
    return ret;
}
