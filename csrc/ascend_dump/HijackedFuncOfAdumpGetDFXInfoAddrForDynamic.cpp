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

#include "ascend_dump/HijackedFunc.h"
#include "runtime/inject_helpers/ArgsManager.h"
#include "utils/InjectLogger.h"

HijackedFuncOfAdumpGetDFXInfoAddrForDynamic::HijackedFuncOfAdumpGetDFXInfoAddrForDynamic()
    : HijackedFuncType("ascend_dump", "AdumpGetDFXInfoAddrForDynamic") {}

void *HijackedFuncOfAdumpGetDFXInfoAddrForDynamic::Call(uint32_t space, uint64_t &atomicIndex) {
    if (this->originfunc_ == nullptr) {
        WARN_LOG("HijackedFuncOfAdumpGetDFXInfoAddrForDynamic originfunc is nullptr.");
        return nullptr;
    }
    void *ret = this->originfunc_(space, atomicIndex);
    if (ret == nullptr) {
        WARN_LOG("AdumpGetDFXInfoAddrForDynamic return nullptr");
    }
    ArgsManager::Instance().AddAdumpInfo(atomicIndex, {ret, space});
    return ret;
}
