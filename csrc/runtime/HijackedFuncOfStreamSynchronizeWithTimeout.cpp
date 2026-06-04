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


#include "HijackedFunc.h"
#include "RuntimeConfig.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/ProfConfig.h"

HijackedFuncOfStreamSynchronizeWithTimeout::HijackedFuncOfStreamSynchronizeWithTimeout()
    : HijackedFuncType(RuntimeLibName(), "rtStreamSynchronizeWithTimeout") { }

rtError_t HijackedFuncOfStreamSynchronizeWithTimeout::Call(rtStream_t stream, int32_t timeout)
{
    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfStreamSynchronizeWithTimeout originfunc is nullptr.");
        return RT_ERROR_RESERVED;
    }

    rtError_t ret = this->originfunc_(stream, -1);
    return Post(ret);
}
