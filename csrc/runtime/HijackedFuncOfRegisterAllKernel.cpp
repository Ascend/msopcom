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

// 该文件主要实现注入函数的功能，其配合被劫持函数的别名，实现新的劫持函数功能

#include "HijackedFunc.h"
#include "utils/FileSystem.h"
#include "utils/InjectLogger.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "RuntimeConfig.h"

HijackedFuncOfRegisterAllKernel::HijackedFuncOfRegisterAllKernel()
    : HijackedFuncOfRegisterAllKernel::HijackedFuncType(
    std::string(RuntimeLibName()), std::string("rtRegisterAllKernel")) {}
void HijackedFuncOfRegisterAllKernel::Pre(const rtDevBinary_t *bin, void **hdl)
{
    this->bin_ = bin;
    this->hdl_ = hdl;
}

rtError_t HijackedFuncOfRegisterAllKernel::Call(const rtDevBinary_t *bin, void **hdl)
{
    Pre(bin, hdl);
    if (originfunc_) {
        rtError_t ret = originfunc_(bin, hdl);
        return Post(ret);
    }
    return EmptyFunc();
}

rtError_t HijackedFuncOfRegisterAllKernel::Post(rtError_t ret)
{
    // 存储kernel、stubkernel和hdl的关系
    if (this->hdl_ == nullptr) {
        ERROR_LOG("rtRegisterAllKernel Hijacked handle is nullptr.");
        return ret;
    }
    KernelContext::Instance().AddHdlRegisterEvent(*this->hdl_, this->bin_, nullptr);
    if (IsOpProf() && ProfConfig::Instance().IsSimulator()) {
        ProfDataCollect::SaveObject(*this->hdl_);
    }
    return ret;
}
