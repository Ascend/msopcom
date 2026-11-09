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

HijackedFuncOfIpcSetMemoryName::HijackedFuncOfIpcSetMemoryName()
    : HijackedFuncType(RuntimeLibName(), "rtIpcSetMemoryName")
{}

rtError_t HijackedFuncOfIpcSetMemoryName::Call(const void *ptr, uint64_t byteCount, char *name, uint32_t len)
{
    std::string nameLog = FormatNameForLog(
        std::string(name, GetValidLength(name, std::min<uint64_t>(len, sizeof(IPCMemorySetInfo::name)))));
    DEBUG_LOG("enter HijackedFuncOfIpcSetMemoryName byteCount:%lu, name:%s, len:%u", byteCount, nameLog.c_str(), len);

    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfIpcSetMemoryName originfunc is nullptr.");
        return RT_ERROR_RESERVED;
    }

    const void *exportPtr = ptr;
    uint64_t exportSize = byteCount;
    GuardBlockInfo blockInfo{};
    // 同 HijackedFuncOfAclrtIpcMemGetExportKeyImpl，只有工具确实扩大过的完整分配才能导出保护区；也避免ACL层已扩长后再次扩长
    bool guarded = MemoryGuard::Instance().memGuardEnable_ &&
        MemoryGuard::Instance().GetGuardBlockInfoByUserAddr(const_cast<void *>(ptr), blockInfo);
    if (guarded && byteCount == blockInfo.userSize) {
        exportPtr = blockInfo.realPtr;
        exportSize += blockInfo.frontGuardSize + blockInfo.backGuardSize;
    }
    DEBUG_LOG("Runtime IPC export user=%p size=%lu guarded=%d actual=%p size=%lu.",
              ptr, byteCount, guarded, exportPtr, exportSize);
    rtError_t ret = originfunc_(exportPtr, exportSize, name, len);

    if (ret != RT_ERROR_NONE) {
        ERROR_LOG("error happened when calling rtIpcSetMemoryName byteCount:%lu, name:%.2048s, len:%u, retVal:%u",
            byteCount, nameLog.c_str(), len, ret);
        return Post(ret);
    }

    if (IsSanitizer()) {
        IPCMemRecord record{};
        record.type = IPCOperationType::SET_INFO;
        record.setInfo.addr = reinterpret_cast<uint64_t>(ptr);
        record.setInfo.size = byteCount;
        uint64_t length = GetValidLength(name, std::min<uint64_t>(len, sizeof(record.setInfo.name)));
        if (length != 0) {
            std::copy_n(name, length, record.setInfo.name);
            record.setInfo.name[length] = '\0';
        }
        IPCInteract(record);
    }

    DEBUG_LOG("leave HijackedFuncOfIpcSetMemoryName byteCount:%lu, name:%.2048s, len:%u, retVal:%u", byteCount,
        nameLog.c_str(), len, ret);

    return Post(ret);
}
