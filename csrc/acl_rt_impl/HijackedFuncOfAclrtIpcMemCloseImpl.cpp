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

#include <string>

#include "core/FuncSelector.h"
#include "runtime/inject_helpers/InteractHelper.h"
#include "runtime/inject_helpers/IPCMemManager.h"
#include "utils/InjectLogger.h"
#include "utils/Protocol.h"
#include "utils/Ustring.h"

HijackedFuncOfAclrtIpcMemCloseImpl::HijackedFuncOfAclrtIpcMemCloseImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtIpcMemCloseImpl") {}

void HijackedFuncOfAclrtIpcMemCloseImpl::Pre(const char *key)
{
    key_ = key;
}

aclError HijackedFuncOfAclrtIpcMemCloseImpl::Post(aclError ret)
{
    if (IsSanitizer()) {
        if (key_ == nullptr) {
            ERROR_LOG("aclrtIpcMemCloseImpl key is nullptr");
            return ret;
        }

        uint64_t length = GetValidLength(key_, sizeof(IPCMemoryDestroyInfo::name));
        std::string key(key_, length);

        // get current process ipc meminfo by this key
        auto it = IPCMemManager::Instance().ipcMemInfoMap.find(key);
        if (it == IPCMemManager::Instance().ipcMemInfoMap.cend()) {
            std::string keyLog = FormatNameForLog(key);
            ERROR_LOG("aclrtIpcMemCloseImpl cannot find key in ipcMemInfoMap. key:%s", keyLog.c_str());
            return ret;
        }

        if (it->second.actor == IPCMemManager::IPCMemActor::SHARER) {
            IPCMemRecord record{};
            record.type = IPCOperationType::DESTROY_INFO;
            std::copy_n(key_, length, record.destroyInfo.name);
            record.destroyInfo.name[length] = '\0';
            IPCMemManager::Instance().ipcMemInfoMap.erase(it);
            IPCInteract(record);
        } else {
            IPCMemRecord record{};
            record.type = IPCOperationType::UNMAP_INFO;
            record.unmapInfo.addr = reinterpret_cast<uint64_t>(it->second.devPtr);
            IPCMemManager::Instance().ipcMemInfoMap.erase(it);
            IPCInteract(record);
        }
    }

    return ret;
}
