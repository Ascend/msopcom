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
#include "utils/InjectLogger.h"
#include "runtime/inject_helpers/ArgsContext.h"
#include "runtime/inject_helpers/FuncContext.h"
#include "runtime/inject_helpers/LaunchContext.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "runtime/inject_helpers/FuncManager.h"

class AclLaunchKernelMixin {
protected:
    FuncContextSP CreateFuncContext(aclrtFuncHandle funcHandle) {
        std::vector<char> name(PATH_MAX, '\0');
        std::string funcName = "";
        if (aclrtGetFunctionNameImplOrigin(funcHandle, PATH_MAX, name.data()) == ACL_ERROR_NONE) {
            funcName = std::string(name.begin(), name.end());
        }
        aclrtBinHandle binHandle = nullptr;
        if (aclrtFunctionGetBinaryImplOrigin(funcHandle, &binHandle) != ACL_ERROR_NONE) {
            binHandle = nullptr;
        }
        if (funcName.empty() || binHandle == nullptr) {
            return nullptr;
        }
        return FuncManager::Instance().CreateContext(binHandle, funcName.c_str(), funcHandle);
    }
    // 归一化 launch 接口第一个入参 func：可能是核函数符号，也可能是 func handle。
    // 新版本 runtime 不区分二者，核函数名称与 func handle 均可直接用于下发，此时 func 可原样使用；
    // 旧版本仅接受 func handle，传入核函数名称会报错。
    // 用 aclrtFunctionGetBinaryImplOrigin 探测当前版本新旧：成功 → 新版本（或 func 本身就是 handle），
    // func 可原样使用；失败 → 旧版本且 func 为核函数名称，用 aclrtGetFuncBySymbol 转成 handle。
    aclrtFuncHandle ResolveFuncHandle(void *func) {
        if (func != nullptr) {
            aclrtBinHandle binHandle = nullptr;
            if (aclrtFunctionGetBinaryImplOrigin(func, &binHandle) == ACL_SUCCESS) {
                DEBUG_LOG("Runtime version is new, use func directly");
                return static_cast<aclrtFuncHandle>(func);
            }
            aclrtFuncHandle fh = nullptr;
            if (aclrtGetFuncBySymbolImplOrigin(func, &fh) == ACL_SUCCESS) {
                DEBUG_LOG("Runtime version is old, resolve kernel name to funchandle");
                return fh;
            }
        }
        DEBUG_LOG("Receive param is nullptr");
        return static_cast<aclrtFuncHandle>(func);
    }
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
