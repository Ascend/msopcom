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

#include <iostream>
#include "HijackedFunc.h"
#include "RuntimeConfig.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/ProfConfig.h"

HijackedFuncOfRegisterFuncSymbol::HijackedFuncOfRegisterFuncSymbol()
    : HijackedFuncType(RuntimeLibName(), "rtRegisterFuncSymbol") {}

rtError_t HijackedFuncOfRegisterFuncSymbol::Call(
    void *binHandle, const void *symbol, const char *kernelName, void *reserve) {
    rtError_t ret = this->originfunc_(binHandle, symbol, kernelName, reserve);
    auto &handleMap = KernelContext::Instance().GetHandleMap();
    if (symbol != nullptr && kernelName != nullptr) {
        handleMap[symbol] = std::pair<void *, std::string>{binHandle, std::string{kernelName}};
    }
    return Post(ret);
}
