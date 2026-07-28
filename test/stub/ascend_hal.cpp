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

#include "ascend_hal.h"
#include "ascend_hal/AscendHalOrigin.h"

using namespace std;

extern "C" {
int prof_drv_start(unsigned int device_id, unsigned int channel_id, struct prof_start_para *start_para)
{
    return -1;
}

int prof_channel_read(unsigned int device_id, unsigned int channel_id, char *out_buf, unsigned int buf_size)
{
    return -1;
}

int prof_stop(unsigned int device_id, unsigned int channel_id)
{
    return -1;
}

int prof_channel_poll(struct prof_poll_info *out_buf, int num, int timeout)
{
    return -1;
}

drvError_t halGetDeviceInfoByBuff(
    uint32_t deviceId, int32_t aicoreType, int32_t frequeType, void* freq, int32_t* size)
{
    return DRV_ERROR_RESERVED;
}

drvError_t halGetDeviceInfo(uint32_t deviceId, int32_t aicoreType, int32_t frequeType, int64_t* freq)
{
    return DRV_ERROR_RESERVED;
}

drvError_t halGetAPIVersion(int *halApiVersion) {
    if (halApiVersion == nullptr) {
        return DRV_ERROR_RESERVED;
    }
    *halApiVersion = 0x072419;
    return DRV_ERROR_NONE;
}
}
