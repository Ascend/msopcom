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

#include "DeviceAttr.h"

#include <limits>

#include "acl_rt_impl/AscendclImplOrigin.h"

namespace {

bool ResolveDeviceId(int32_t devId, uint32_t &deviceId) {
    if (devId >= 0) {
        deviceId = static_cast<uint32_t>(devId);
        return true;
    }
    int32_t currentDeviceId = -1;
    if (aclrtGetDeviceImplOrigin(&currentDeviceId) != ACL_SUCCESS || currentDeviceId < 0) {
        return false;
    }
    deviceId = static_cast<uint32_t>(currentDeviceId);
    return true;
}

bool QueryAttribute(int32_t devId, aclrtDevAttr attr, int64_t &value) {
    uint32_t deviceId = 0;
    if (!ResolveDeviceId(devId, deviceId)) {
        return false;
    }
    int64_t result = 0;
    if (aclrtGetDeviceInfoImplOrigin(deviceId, attr, &result) != ACL_SUCCESS || result <= 0) {
        return false;
    }
    value = result;
    return true;
}

} // namespace

namespace DeviceAttr {

bool QueryAicoreCoreNum(int32_t devId, uint32_t &coreNum) {
    int64_t value = 0;
    if (!QueryAttribute(devId, ACL_DEV_ATTR_AICORE_CORE_NUM, value) || value > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    coreNum = static_cast<uint32_t>(value);
    return true;
}

bool QueryL2CacheSize(int32_t devId, uint64_t &sizeBytes) {
    int64_t value = 0;
    if (!QueryAttribute(devId, ACL_DEV_ATTR_L2_CACHE_SIZE, value)) {
        return false;
    }
    sizeBytes = static_cast<uint64_t>(value);
    return true;
}

} // namespace DeviceAttr
