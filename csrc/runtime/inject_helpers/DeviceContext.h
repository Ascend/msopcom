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

#ifndef __DEVICE_CONTEXT_H__
#define __DEVICE_CONTEXT_H__

#pragma once

#include <string>
#include <atomic>
#include "acl.h"

/*
 * 管理Device信息，每张卡有一个实例，不应该对业务逻辑有依赖。
 */
class DeviceContext {
public:
    // 获取当前激活的设备上下文
    static DeviceContext &Local();
    // 获取指定 deviceId 的设备上下文
    static DeviceContext &Instance(int32_t deviceId);

    void SetSocVersion(const std::string &name) {socVersion_ = name;}

    const std::string &GetSocVersion();

    void SetDeviceId(int32_t devId);

    int32_t GetDeviceId() const;

    int32_t GetVisibleDeviceId() const { return visibleDevId_; }

    static int32_t GetRunningDeviceId();

    // 普通reset成功可能仅减少引用，默认Context仍活跃时不能撤销设备内存记录
    static bool IsPrimaryContextReleased(int32_t deviceId);

    // 默认Context最终释放后推进代际，避免新Context重用相同指针命中旧设备函数
    void AdvanceContextGeneration() { contextGeneration_.fetch_add(1, std::memory_order_relaxed); }
    uint64_t GetContextGeneration() const { return contextGeneration_.load(std::memory_order_relaxed); }

    bool NeedOverflowStatus() const;

    bool IsAscend950();

#if defined (__BUILD_TESTS__)
    void Clear()
    {
        socVersion_ = "";
        devId_ = 0;
        visibleDevId_ = 0;
    }
#endif
private:
    std::atomic<uint64_t> contextGeneration_{0};
    std::string socVersion_;
    int32_t visibleDevId_{0};
    int32_t devId_{0};
};

#endif // __DEVICE_CONTEXT_H__
