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
        void *exportPtr = devPtr;
        size_t exportSize = size;
        GuardBlockInfo blockInfo{};
        // HCCL 使用的内存可能没有经过安全区扩展，也可能只导出一个子区间，或者请求已经被上层扩展过，再加一次就会超过真实分配范围
        // 修改为 起始地址命中工具登记的用户分配 + 导出大小等于该分配完整的用户大小 两个条件都满足时再扩展
        bool guarded = MemoryGuard::Instance().memGuardEnable_ &&
            MemoryGuard::Instance().GetGuardBlockInfoByUserAddr(devPtr, blockInfo);
        if (guarded && size == blockInfo.userSize) {
            exportPtr = blockInfo.realPtr;
            exportSize += blockInfo.frontGuardSize + blockInfo.backGuardSize;
        }
        DEBUG_LOG("IPC export user=%p size=%zu guarded=%d actual=%p size=%zu.",
                  devPtr, size, guarded, exportPtr, exportSize);
        aclError ret = originfunc_(exportPtr, exportSize, key, len, flag);
        DEBUG_LOG("IPC export result=%d user=%p size=%zu.", ret, devPtr, size);
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
