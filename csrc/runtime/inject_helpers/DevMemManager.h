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


#pragma once

#include "acl.h"
#include "utils/Singleton.h"

class DevMemManager : public Singleton<DevMemManager, true> {
public:
    friend class Singleton<DevMemManager, true>;

    aclError MallocMemory(void** memPtr, uint64_t size);
    void Free();
    void SetMemoryInitFlag(bool flag) { isMemoryInit_ = flag; }
    bool IsMemoryInit() const { return isMemoryInit_; }
    void SetSkipKernelFlag(bool flag) { isSkipKernel_ = flag; }
    bool IsSkipKernel() const { return isSkipKernel_; }

private:
    DevMemManager() = default;
    ~DevMemManager() { this->Free(); }

private:
    uint64_t memSize_ = 0;
    void *memPtr_ = nullptr;
    bool isMemoryInit_ = false;
    bool isSkipKernel_ = false;
    int32_t deviceId_ = -1;
};
