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

#include "core/FuncSelector.h"
#include "runtime/inject_helpers/InteractHelper.h"
#include "runtime/inject_helpers/IPCMemManager.h"
#include "runtime/inject_helpers/MemGuard.h"
#include "utils/InjectLogger.h"
#include "utils/Protocol.h"
#include "utils/Ustring.h"

HijackedFuncOfAclrtIpcMemImportByKeyImpl::HijackedFuncOfAclrtIpcMemImportByKeyImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtIpcMemImportByKeyImpl") {}

void HijackedFuncOfAclrtIpcMemImportByKeyImpl::Pre(void **devPtr, const char *key, uint64_t flag)
{
    devPtr_ = devPtr;
    key_ = key;
}

aclError HijackedFuncOfAclrtIpcMemImportByKeyImpl::Call(void **devPtr, const char *key, uint64_t flag)
{
    Pre(devPtr, key, flag);
    if (originfunc_) {
        aclError ret = originfunc_(devPtr, key, flag);
        if (MemoryGuard::Instance().memGuardEnable_) {
            size_t frontSize = 0;
            size_t backSize = 0;
            MemoryGuard::Instance().GetGuardSizes(frontSize, backSize);
            *devPtr = reinterpret_cast<char *>(*devPtr) + frontSize;    // 偏移到用户地址再返回
        }

        return Post(ret);
    }
    ERROR_LOG("HijackedFuncOfAclrtIpcMemImportByKeyImpl originfunc is nullptr.");

    return EmptyFunc();
}

aclError HijackedFuncOfAclrtIpcMemImportByKeyImpl::Post(aclError ret)
{
    if (IsSanitizer()) {
        if (key_ == nullptr) {
            ERROR_LOG("aclrtIpcMemImportByKeyImpl key is nullptr");
            return ret;
        }

        uint64_t length = GetValidLength(key_, sizeof(IPCMemoryMapInfo::name));
        std::string key(key_, length);

        if (devPtr_ == nullptr) {
            ERROR_LOG("aclrtIpcMemImportByKeyImpl return nullptr key:%.2048s.", ToSafeString(key).c_str());
            return ret;
        }

        // current process is sharee by this key
        IPCMemManager::IPCMemInfo ipcMemInfo{IPCMemManager::IPCMemActor::SHAREE, *devPtr_};
        IPCMemManager::Instance().ipcMemInfoMap.insert({key, ipcMemInfo});

        IPCMemRecord record{};
        record.type = IPCOperationType::MAP_INFO;
        record.mapInfo.addr = reinterpret_cast<uint64_t>(*devPtr_);
        std::copy_n(key_, length, record.mapInfo.name);
        record.mapInfo.name[length] = '\0';
        IPCInteract(record);
    }

    return ret;
}
