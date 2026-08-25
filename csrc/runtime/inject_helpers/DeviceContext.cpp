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

#include "DeviceContext.h"

#include <unordered_map>
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "utils/InjectLogger.h"
#include "core/FuncSelector.h"
#include "ProfConfig.h"
#include "ThreadContext.h"

using namespace std;

DeviceContext &DeviceContext::Local()
{
    return Instance(ThreadContext::Instance().GetDeviceId());
}

DeviceContext &DeviceContext::Instance(int32_t deviceId)
{
    static unordered_map<int32_t, DeviceContext> insts;
    static mutex mtx;
    lock_guard<mutex> guard(mtx);
    return insts[deviceId];
}

const std::string &DeviceContext::GetSocVersion()
{
    if (!socVersion_.empty()) {
        return socVersion_;
    }
    char const *name = aclrtGetSocNameImplOrigin();
    if (name) {
        socVersion_ = name;
    }
    return socVersion_;
}

inline void ConvertToVisibleDeviceIdIfPossible(int32_t &deviceId)
{
    void *func = GET_FUNCTION("runtime", "rtGetVisibleDeviceIdByLogicDeviceId");
    if (func == nullptr) {
        return;
    }
    int32_t convertedId = 0;
    // device id captured in hijacked reSetDevice is not always correct if
    // ASCEND_RT_VISIBLE_DEVICES is set. conversion needs to be done here.
    auto ret = aclrtGetLogicDevIdByUserDevIdImplOrigin(deviceId, &convertedId);
    if (ret != ACL_ERROR_NONE) {
        DEBUG_LOG("Get visible device id from %d fail, ret: %d.", deviceId, ret);
        return;
    }
    DEBUG_LOG("DeviceId convert id %d to %d, ret : %d", deviceId, convertedId, ret);
    deviceId = convertedId;
}

void DeviceContext::SetDeviceId(int32_t devId)
{
    // 比较难改，考虑到兼容性，先不动，避免修改出问题。这种业务逻辑将来需要移出去。
    if (IsOpProf() &&  ProfConfig::Instance().IsSimulator()) {
        devId_ = devId;
        visibleDevId_ = devId;
        return;
    }
    int32_t convertedId = devId;
    ConvertToVisibleDeviceIdIfPossible(convertedId);
    visibleDevId_ = convertedId;
    devId_ = devId;
}

int32_t DeviceContext::GetDeviceId() const
{
    return devId_;
}

int32_t DeviceContext::GetRunningDeviceId()
{
    int32_t deviceId = 0;
    auto ret = aclrtGetDeviceImplOrigin(&deviceId);
    if (ret != ACL_SUCCESS) {
        DEBUG_LOG("Get running device id failed, ret: %d.", static_cast<int32_t>(ret));
        return deviceId;
    }
    int32_t runningDeviceId = deviceId;
    ConvertToVisibleDeviceIdIfPossible(runningDeviceId);
    return runningDeviceId;
}

bool DeviceContext::NeedOverflowStatus() const
{
    if (socVersion_.find("Ascend310P") != std::string::npos || socVersion_.find("Ascend310p") != std::string::npos) {
        return true;
    }
    return false;
}
