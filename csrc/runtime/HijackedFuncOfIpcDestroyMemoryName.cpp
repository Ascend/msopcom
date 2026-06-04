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

#include <algorithm>
#include <iterator>
#include "HijackedFunc.h"
#include "RuntimeConfig.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/InteractHelper.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "utils/Ustring.h"

HijackedFuncOfIpcDestroyMemoryName::HijackedFuncOfIpcDestroyMemoryName()
    : HijackedFuncType(RuntimeLibName(), "rtIpcDestroyMemoryName") {}


rtError_t HijackedFuncOfIpcDestroyMemoryName::Call(const char *name)
{
    uint64_t validLen = GetValidLength(name, KERNEL_NAME_MAX);
    std::string validName(name, validLen);
    DEBUG_LOG("enter HijackedFuncOfIpcDestroyMemoryName name: %s", validName.c_str());

    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfIpcDestroyMemoryName originfunc is nullptr.");
        return Post(RT_ERROR_RESERVED);
    }

    rtError_t ret = this->originfunc_(name);
    if (ret != RT_ERROR_NONE) {
        ERROR_LOG("error happened when calling rtIpcDestroyMemoryName, name:%s, ", name);
        return Post(ret);
    }

    if (IsSanitizer()) {
        IPCMemRecord record{};
        record.type = IPCOperationType::DESTROY_INFO;
        uint64_t length = GetValidLength(name, sizeof(record.destroyInfo.name));
        if (length != 0) {
            std::copy_n(name, length, record.destroyInfo.name);
            record.destroyInfo.name[length] = '\0';
        }

        IPCInteract(record);
    }

    DEBUG_LOG("leave HijackedFuncOfIpcDestroyMemoryName name:(%.2048s).", name);

    return Post(ret);
}
