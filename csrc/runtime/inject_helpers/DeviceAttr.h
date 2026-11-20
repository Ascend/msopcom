/* -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
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

#ifndef __DEVICE_ATTR_H__
#define __DEVICE_ATTR_H__

#pragma once

#include <cstdint>

/*
 * 设备属性查询：通过原始运行时接口获取设备规格。
 *
 * 约定：
 * 1. devId 传负值表示"取 runtime 当前设备"，获取失败返回 false；
 * 2. 查询失败或返回非法值（<=0）时返回 false，不返回半成品数据；
 * 3. 不缓存属性值，每次调用都会真实发起一次设备查询；
 */
namespace DeviceAttr {

/// NPU_ARCH 架构号
constexpr int64_t NPU_ARCH_C220 = 2201; // 910B / 910_93 系（c220）
constexpr int64_t NPU_ARCH_C310 = 3510; // 950 系（c310）

/// 查询设备 NPU_ARCH 架构号
bool QueryNpuArch(int32_t devId, int64_t &arch);

/// 查询设备 AI Core 数量
bool QueryAicoreCoreNum(int32_t devId, uint32_t &coreNum);

/// 查询整卡 L2 Cache 容量，单位字节
bool QueryL2CacheSize(int32_t devId, uint64_t &sizeBytes);

} // namespace DeviceAttr

#endif // __DEVICE_ATTR_H__
