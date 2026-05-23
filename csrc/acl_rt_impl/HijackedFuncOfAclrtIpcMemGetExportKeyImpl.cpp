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

HijackedFuncOfAclrtIpcMemGetExportKeyImpl::HijackedFuncOfAclrtIpcMemGetExportKeyImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtIpcMemGetExportKeyImpl") {}

void HijackedFuncOfAclrtIpcMemGetExportKeyImpl::Pre(void *devPtr, size_t size, char *key,
                                                    size_t len, uint64_t flag)
{
    devPtr_ = devPtr;
    size_ = size;
    key_ = key;
    len_ = len;
}

aclError HijackedFuncOfAclrtIpcMemGetExportKeyImpl::Call(void *devPtr, size_t size, char *key, size_t len, uint64_t flag)
{
    Pre(devPtr, size, key, len, flag);
    
    if (originfunc_) {
        aclError ret;
        if (MemoryGuard::Instance().memGuardEnable_) {
            size_t frontSize = 0;
            size_t backSize = 0;
            MemoryGuard::Instance().GetGuardSizes(frontSize, backSize);
            // 还原为实际地址再调用
            ret = originfunc_(reinterpret_cast<char *>(devPtr) - frontSize, size + frontSize + backSize, key, len, flag);
        } else {
            ret = originfunc_(devPtr, size, key, len, flag);
        }
        
        return Post(ret);
    }
    ERROR_LOG("HijackedFuncOfAclrtIpcMemGetExportKeyImpl originfunc is nullptr.");

    return EmptyFunc();
}

aclError HijackedFuncOfAclrtIpcMemGetExportKeyImpl::Post(aclError ret)
{
    if (ret != ACL_ERROR_NONE) {
        return ret;
    }

    if (IsSanitizer()) {
        if (key_ == nullptr) {
            ERROR_LOG("aclrtIpcMemGetExportKeyImpl key is nullptr");
            return ret;
        }

        uint64_t length = GetValidLength(key_, std::min<uint64_t>(len_, sizeof(IPCMemorySetInfo::name)));
        std::string key(key_, length);

        // current process is sharer by this key
        IPCMemManager::IPCMemInfo ipcMemInfo{IPCMemManager::IPCMemActor::SHARER, devPtr_};
        IPCMemManager::Instance().ipcMemInfoMap.insert({key, ipcMemInfo});

        IPCMemRecord record{};
        record.type = IPCOperationType::SET_INFO;
        record.setInfo.addr = reinterpret_cast<uint64_t>(devPtr_);
        record.setInfo.size = size_;
        std::copy_n(key_, length, record.setInfo.name);
        record.setInfo.name[length] = '\0';
        IPCInteract(record);
    }

    return ret;
}
