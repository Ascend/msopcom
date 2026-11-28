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

#include "LocalDevice.h"

#include <mutex>
#include <memory>
#include "runtime/inject_helpers/ThreadContext.h"

LocalDevice &LocalDevice::Local(CommType type)
{
    return GetInstance(ThreadContext::Instance().GetDeviceId(), type);
}

// 按设备号查进程连接表，首次出现时创建连接
LocalDevice &LocalDevice::GetInstance(int32_t deviceId, CommType type)
{
    auto &state = State(); // 首次访问时创建进程共享状态
    CheckProcessOwner(); // 子进程不能沿用父进程的 fd 和锁
    std::lock_guard<std::mutex> guard(state.devicesMutex);
    auto it = state.devices.find(deviceId);
    if (it == state.devices.end()) {
        it = state.devices.emplace(deviceId, std::unique_ptr<LocalDevice>(new LocalDevice(type))).first;
    }
    return *it->second;
}
