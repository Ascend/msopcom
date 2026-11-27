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

#include <cstdint>

#include "acl_rt_impl/AscendclImplOrigin.h"
#include "utils/Singleton.h"
#include "utils/InjectLogger.h"

// 保存线程到设备逻辑 id 的映射关系
class ThreadContext : public Singleton<ThreadContext, true> {
public:
    friend class Singleton<ThreadContext, true>;

    void SetDeviceId(int32_t deviceId) { deviceId_ = deviceId; }
    int32_t GetDeviceId() {
        auto ret = aclrtGetDeviceImplOrigin(&deviceId_);
        if (ret != ACL_SUCCESS) {
            DEBUG_LOG("Get device id failed. error code: %d", ret);
        }
        return deviceId_;
    }

private:
    int32_t deviceId_{};
};
