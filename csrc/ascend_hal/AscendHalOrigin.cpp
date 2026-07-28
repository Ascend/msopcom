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

#include "AscendHalOrigin.h"
#include "core/FunctionLoader.h"

/// fake implement of hal interfaces
#undef LOAD_FUNCTION
#define LOAD_FUNCTION(funcName) REGISTER_FUNCTION("ascend_hal", funcName)

#define LOAD_FUNCTION_BODY(funcName, suffix, ErrorRet, ...) \
    FUNC_BODY("ascend_hal", funcName, suffix, ErrorRet, __VA_ARGS__)
REGISTER_LIBRARY("ascend_hal");

LOAD_FUNCTION(prof_drv_start);
int prof_drv_start_origin(unsigned int device_id, unsigned int channel_id, struct prof_start_para *start_para) {
    LOAD_FUNCTION_BODY(prof_drv_start, _origin, -1, device_id, channel_id, start_para);
}

LOAD_FUNCTION(prof_channel_read);
int prof_channel_read_origin(unsigned int device_id, unsigned int channel_id, char *out_buf, unsigned int buf_size) {
    LOAD_FUNCTION_BODY(prof_channel_read, _origin, -1, device_id, channel_id, out_buf, buf_size);
}

LOAD_FUNCTION(prof_stop);
int prof_stop_origin(unsigned int device_id, unsigned int channel_id) {
    LOAD_FUNCTION_BODY(prof_stop, _origin, -1, device_id, channel_id);
}

LOAD_FUNCTION(prof_channel_poll);
int prof_channel_poll_origin(struct prof_poll_info *out_buf, int num, int timeout) {
    LOAD_FUNCTION_BODY(prof_channel_poll, _origin, -1, out_buf, num, timeout);
}

LOAD_FUNCTION(halGetDeviceInfoByBuff);
drvError_t halGetDeviceInfoByBuffOrigin(
    uint32_t deviceId, int32_t aicoreType, int32_t frequeType, void *freq, int32_t *size) {
    LOAD_FUNCTION_BODY(
        halGetDeviceInfoByBuff, Origin, DRV_ERROR_RESERVED, deviceId, aicoreType, frequeType, freq, size);
}

LOAD_FUNCTION(halGetDeviceInfo);
drvError_t halGetDeviceInfoOrigin(uint32_t deviceId, int32_t aicoreType, int32_t frequeType, int64_t *freq) {
    LOAD_FUNCTION_BODY(halGetDeviceInfo, Origin, DRV_ERROR_RESERVED, deviceId, aicoreType, frequeType, freq);
}

LOAD_FUNCTION(halGetAPIVersion);
drvError_t halGetAPIVersionOrigin(int32_t *halApiVersion) {
    LOAD_FUNCTION_BODY(halGetAPIVersion, Origin, DRV_ERROR_RESERVED, halApiVersion);
}

LOAD_FUNCTION(drvMemGetAttribute);
drvError_t drvMemGetAttributeOrigin(DVdeviceptr vptr, struct DVattribute *attr) {
    LOAD_FUNCTION_BODY(drvMemGetAttribute, Origin, DRV_ERROR_RESERVED, vptr, attr);
}
