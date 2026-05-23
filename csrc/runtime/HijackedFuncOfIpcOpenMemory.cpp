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
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/InteractHelper.h"
#include "runtime/inject_helpers/MemGuard.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "utils/Ustring.h"

HijackedFuncOfIpcOpenMemory::HijackedFuncOfIpcOpenMemory()
    : HijackedFuncType(RuntimeLibName(), "rtIpcOpenMemory") {}

rtError_t HijackedFuncOfIpcOpenMemory::Call(void **ptr, const char *name)
{
    DEBUG_LOG("enter HijackedFuncOfIpcOpenMemory name:%.2048s.", name);

    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfIpcOpenMemory originfunc is nullptr.");
        return RT_ERROR_RESERVED;
    }
    rtError_t ret = this->originfunc_(ptr, name);
    if (ret != RT_ERROR_NONE) {
        ERROR_LOG("error happened when calling rtIpcOpenMemory name:%.2048s.", name);
        return ret;
    }

    if (MemoryGuard::Instance().memGuardEnable_) {
        size_t frontSize = 0;
        size_t backSize = 0;
        MemoryGuard::Instance().GetGuardSizes(frontSize, backSize);
        // 偏移到用户地址再返回
        *ptr = reinterpret_cast<char *>(*ptr) + frontSize;
    }

    if (IsSanitizer()) {
        if (ptr == nullptr) {
            ERROR_LOG("rtIpcOpenMemory return nullptr name:%.2048s.", name);
        } else {
            IPCMemRecord record{};
            record.type = IPCOperationType::MAP_INFO;
            record.mapInfo.addr = reinterpret_cast<uint64_t>(*ptr);
            uint64_t length = GetValidLength(name, sizeof(record.mapInfo.name));
            if (length != 0) {
                std::copy_n(name, length, record.mapInfo.name);
                record.mapInfo.name[length] = '\0';
            }
            IPCInteract(record);
        }
    }

    DEBUG_LOG("leave HijackedFuncOfIpcOpenMemory name:%.2048s.", name);

    return Post(ret);
}
