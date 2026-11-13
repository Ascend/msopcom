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

#ifndef __UTILS_DEVICE_ATTR_H__
#define __UTILS_DEVICE_ATTR_H__

#include <cstdint>

// 清 L2 所需的实际设备规格，与基于型号字符串的系列识别无关。
// devId 为负时查询 runtime 当前设备，设备或属性查询失败时返回 false。
namespace DeviceAttr {

bool QueryAicoreCoreNum(int32_t devId, uint32_t &coreNum);

bool QueryL2CacheSize(int32_t devId, uint64_t &sizeBytes);

} // namespace DeviceAttr

#endif // __UTILS_DEVICE_ATTR_H__
