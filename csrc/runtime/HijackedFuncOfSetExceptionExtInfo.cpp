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

#include "core/FuncSelector.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "RuntimeConfig.h"

HijackedFuncOfSetExceptionExtInfo::HijackedFuncOfSetExceptionExtInfo()
    : HijackedFuncOfSetExceptionExtInfo::HijackedFuncType(
    std::string(RuntimeLibName()), std::string("rtSetExceptionExtInfo")) {}

void HijackedFuncOfSetExceptionExtInfo::Pre(const rtArgsSizeInfo_t * const sizeInfo)
{
    KernelContext::Instance().SetArgsSize(sizeInfo);
}

rtError_t HijackedFuncOfSetExceptionExtInfo::Post(rtError_t ret)
{
    return ret;
}
