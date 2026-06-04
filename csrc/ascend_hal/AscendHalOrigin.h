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

#ifndef __MSOPPROF_ASCEND_HAL_H__
#define __MSOPPROF_ASCEND_HAL_H__

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define PROF_REAL 1
#define DV_MEM_RESV 8
#define CHANNEL_AICORE (43)
#define CHANNEL_HWTS_LOG (45) /* add for ts0 as hwts channel */
#define CHANNEL_TSFW_L2 (47)
#define CHANNEL_STARS_SOC_LOG_BUFFER (50) /* add for ascend910B */
#define CHANNEL_FFTS_PROFILE_BUFFER_TASK (53) /* add for ascend910B */
#define CHANNEL_AICPU (143)
#define CHANNEL_HCCS (9)

typedef void *DVdeviceptr;

enum class InstrChannel : uint32_t {
    GROUP0_AIC = 11,
    GROUP0_AIV0,
    GROUP0_AIV1,
    GROUP1_AIC,
    GROUP1_AIV0,
    GROUP1_AIV1,
    GROUP2_AIC,
    GROUP2_AIV0,
    GROUP2_AIV1,
    GROUP3_AIC,
    GROUP3_AIV0,
    GROUP3_AIV1,
    GROUP4_AIC,
    GROUP4_AIV0,
    GROUP4_AIV1,
    GROUP5_AIC,
    GROUP5_AIV0,
    GROUP5_AIV1,
};

typedef enum {
    MODULE_TYPE_SYSTEM = 0, /**< system info*/
    MODULE_TYPE_AICPU, /** < aicpu info*/
    MODULE_TYPE_CCPU, /**< ccpu_info*/
    MODULE_TYPE_DCPU, /**< dcpu info*/
    MODULE_TYPE_AICORE, /**< AI CORE info*/
    MODULE_TYPE_TSCPU, /**< tscpu info*/
    MODULE_TYPE_PCIE, /**< PCIE info*/
    MODULE_TYPE_VECTOR_CORE, /**< VECTOR CORE info*/
    MODULE_TYPE_COMPUTING = 0x8000, /* computing power info */
} DEV_MODULE_TYPE;

typedef enum prof_channel_type {
    PROF_TS_TYPE,
    PROF_PERIPHERAL_TYPE,
    PROF_CHANNEL_TYPE_MAX,
} PROF_CHANNEL_TYPE;

typedef struct prof_poll_info {
    unsigned int device_id;
    unsigned int channel_id;
} prof_poll_info_t;

typedef struct prof_start_para {
    PROF_CHANNEL_TYPE channel_type; /* for ts and other device */
    unsigned int sample_period;
    unsigned int real_time; /* real mode */
    void *user_data; /* ts data's pointer */
    unsigned int user_data_size; /* user data's size */
} prof_start_para_t;

typedef enum tagDrvError {
    DRV_ERROR_NONE = 0, /**< success */
    DRV_ERROR_NOT_SUPPORT = 0xfffe,
    DRV_ERROR_RESERVED
} drvError_t;

typedef enum {
    INFO_TYPE_ENV = 0,
    INFO_TYPE_VERSION,
    INFO_TYPE_MASTERID,
    INFO_TYPE_CORE_NUM,
    INFO_TYPE_FREQUE,
    INFO_TYPE_OS_SCHED,
    INFO_TYPE_IN_USED,
    INFO_TYPE_ERROR_MAP,
    INFO_TYPE_OCCUPY,
    INFO_TYPE_ID,
    INFO_TYPE_IP,
    INFO_TYPE_ENDIAN,
    INFO_TYPE_P2P_CAPABILITY,
    INFO_TYPE_SYS_COUNT,
    INFO_TYPE_MONOTONIC_RAW,
    INFO_TYPE_CORE_NUM_LEVEL,
    INFO_TYPE_FREQUE_LEVEL,
    INFO_TYPE_FFTS_TYPE,
    INFO_TYPE_PHY_CHIP_ID,
    INFO_TYPE_PHY_DIE_ID,
    INFO_TYPE_PF_CORE_NUM,
    INFO_TYPE_PF_OCCUPY,
    INFO_TYPE_WORK_MODE,
    INFO_TYPE_UTILIZATION,
    INFO_TYPE_HOST_OSC_FREQUE,
    INFO_TYPE_DEV_OSC_FREQUE,
    INFO_TYPE_SDID,
    INFO_TYPE_SERVER_ID,
    INFO_TYPE_SCALE_TYPE,
    INFO_TYPE_SUPER_POD_ID,
    INFO_TYPE_ADDR_MODE,
    INFO_TYPE_RUN_MACH,
    INFO_TYPE_CURRENT_FREQ,
    INFO_TYPE_CONFIG,
    INFO_TYPE_UCE_VA,
    INFO_TYPE_HOST_KERN_LOG,
} DEV_INFO_TYPE;

typedef enum tagMemType {
    DV_MEM_SVM = 0x0001, /**< DV_MEM_SVM_DEVICE : svm memory & mapped device */
    DV_MEM_SVM_HOST = 0x0002, /**< DV_MEM_SVM_HOST : svm memory & mapped host */
    DV_MEM_SVM_DEVICE = 0x0004, /**< DV_MEM_SVM : svm memory & no mapped */
    DV_MEM_LOCK_HOST = 0x0008, /**< DV_MEM_LOCK_HOST :    host mapped memory & lock host */
    DV_MEM_LOCK_DEV = 0x0010, /**< DV_MEM_LOCK_DEV : dev mapped memory & lock dev */
    DV_MEM_LOCK_DEV_DVPP = 0x0020, /**< DV_MEM_LOCK_DEV_DVPP : dev_dvpp mapped memory & lock dev */
} DV_MEM_TYPE;

struct DVattribute {
    uint32_t memType;
    uint32_t resv1;
    uint32_t resv2;

    uint32_t devId;
    uint32_t pageSize;
    uint32_t resv[DV_MEM_RESV];
};

int prof_drv_start_origin(unsigned int device_id, unsigned int channel_id, struct prof_start_para *start_para);

int prof_channel_read_origin(unsigned int device_id, unsigned int channel_id, char *out_buf, unsigned int buf_size);

int prof_stop_origin(unsigned int device_id, unsigned int channel_id);

int prof_channel_poll_origin(struct prof_poll_info *out_buf, int num, int timeout);

drvError_t halGetDeviceInfoByBuffOrigin(
    uint32_t deviceId, int32_t aicoreType, int32_t frequeType, void *freq, int32_t *size);

drvError_t halGetDeviceInfoOrigin(uint32_t deviceId, int32_t aicoreType, int32_t frequeType, int64_t *freq);

drvError_t drvMemGetAttributeOrigin(DVdeviceptr vptr, struct DVattribute *attr);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // __MSOPPROF_ASCEND_HAL_H__
