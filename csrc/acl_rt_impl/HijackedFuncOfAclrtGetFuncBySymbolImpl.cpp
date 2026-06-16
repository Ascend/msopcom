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
#include "runtime/inject_helpers/KernelContext.h"
#include "utils/InjectLogger.h"

HijackedFuncOfAclrtGetFuncBySymbolImpl::HijackedFuncOfAclrtGetFuncBySymbolImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtGetFuncBySymbolImpl") {}

void HijackedFuncOfAclrtGetFuncBySymbolImpl::Pre(const void *symbol, aclrtFuncHandle *funcHandle)
{
    symbol_ = symbol;
    funcHandle_ = funcHandle;
}

aclError HijackedFuncOfAclrtGetFuncBySymbolImpl::Post(aclError ret)
{
    auto &handleMap = KernelContext::Instance().GetHandleMap();
    if (symbol_ != nullptr && funcHandle_ != nullptr && *funcHandle_ != nullptr &&
        handleMap.find(symbol_) != handleMap.end()) {
        auto ctx = FuncManager::Instance().CreateContext(
            handleMap.at(symbol_).first, handleMap.at(symbol_).second.c_str(), *funcHandle_);
        if (!ctx) {
            WARN_LOG("Failed to create func context");
        }
    }
    return ret;
}
