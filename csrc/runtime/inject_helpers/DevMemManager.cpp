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

#include "acl.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "utils/InjectLogger.h"

#include "DevMemManager.h"

aclError DevMemManager::MallocMemory(void **memPtr, uint64_t size)
{
    if (memPtr == nullptr) {
        return ACL_ERROR_BAD_ALLOC;
    }

    int32_t deviceId {};
    if (aclrtGetDeviceImplOrigin(&deviceId) != ACL_ERROR_NONE) {
        return ACL_ERROR_BAD_ALLOC;
    }

    if (deviceId == this->deviceId_ && memSize_ >= size) {
        *memPtr = memPtr_;
        return ACL_ERROR_NONE;
    }

    Free();
    aclError error = aclrtMallocImplOrigin(&memPtr_, size, ACL_MEM_MALLOC_HUGE_FIRST);
    if (error != ACL_ERROR_NONE) {
        return error;
    }
    *memPtr = memPtr_;
    memSize_ = size;
    /// 重新申请内存时，重置isMemoryInit_标志位
    isMemoryInit_ = false;
    deviceId_ = deviceId;
    return ACL_ERROR_NONE;
}

void DevMemManager::Free()
{
    // rtFree不会将指针置成空指针，需要自行重设
    if (memPtr_ != nullptr) {
        aclrtFreeImplOrigin(memPtr_);
        memPtr_ = nullptr;
    }
    memSize_ = 0;
    deviceId_ = -1;
}
