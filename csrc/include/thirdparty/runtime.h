/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2021. All rights reserved.
 * Description: kernel.h
 * Create: 2020-01-01
 */

// 这个文件本身需要与被劫持的对象一样（为方便后续区分acl等劫持，此处包含cann包的runtime内多个文件定义内容）

#ifndef __RUNTIME_H__
#define __RUNTIME_H__

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RTS_API
#define RTS_API
#endif  // RTS_API


/**
  * @ingroup rt_kernel
  * @brief magic number of elf binary for aicore
  */
#define RT_DEV_BINARY_MAGIC_ELF 0x43554245

/**
 * @ingroup rt_kernel
 * @brief magic number of elf binary for aicpu
 */
#define RT_DEV_BINARY_MAGIC_ELF_AICPU 0x41415243

/**
 * @ingroup rt_kernel
 * @brief magic number of elf binary for aivector
 */
#define RT_DEV_BINARY_MAGIC_ELF_AIVEC 0x41415246

/**
 * @ingroup rt_kernel
 * @brief magic number of elf binary for aicube
 */
#define RT_DEV_BINARY_MAGIC_ELF_AICUBE 0x41494343U

/**
 * @ingroup dvrt_mem
 * @brief memory type | memory Policy
 */
// memory type
typedef enum TagRtMemType {
    RT_MEMORY_DEFAULT = 0x0, /* default memory on device */
    RT_MEMORY_HBM = 0x1, /* HBM memory on device */
    RT_MEMORY_DDR = 0x2, /* DDR memory on device */
    RT_MEMORY_SPM = 0x3, /* shared physical memory on device */
    RT_MEMORY_P2P_HBM = 0x10,
    RT_MEMORY_P2P_DDR = 0x11,
    RT_MEMORY_DDR_NC = 0x20,
    RT_MEMORY_RESERVED,
} rtMemType_t;

/**
 * @ingroup dvrt_base
 * @brief runtime error numbers.
 */
typedef enum tagRtError {
    RT_ERROR_NONE = 0x0,                      // success
    RT_ERROR_INVALID_VALUE = 0x1,             // invalid value
    RT_ERROR_MEMORY_ALLOCATION = 0x2,         // memory allocation fail
    RT_ERROR_INVALID_RESOURCE_HANDLE = 0x3,   // invalid handle
    RT_ERROR_INVALID_DEVICE_POINTER = 0x4,    // invalid device point
    RT_ERROR_INVALID_MEMCPY_DIRECTION = 0x5, // invalid memory copy direction
    RT_ERROR_INVALID_DEVICE = 0x6,            // invalid device
    RT_ERROR_NO_DEVICE = 0x7,                 // no valid device
    RT_ERROR_CMD_OCCUPY_FAILURE = 0x8,        // command occpuy failure
    RT_ERROR_SET_SIGNAL_FAILURE = 0x9,        // set signal failure
    RT_ERROR_UNSET_SIGNAL_FAILURE = 0xA,      // unset signal failure
    RT_ERROR_OPEN_FILE_FAILURE = 0xB,         // unset signal failure
    RT_ERROR_WRITE_FILE_FAILURE = 0xC,
    RT_ERROR_MEMORY_ADDRESS_UNALIGNED = 0xD,
    RT_ERROR_DRV_ERR = 0xE,
    RT_ERROR_LOST_HEARTBEAT = 0xF,
    RT_ERROR_REPORT_TIMEOUT = 0x10,
    RT_ERROR_NOT_READY = 0x11,
    RT_ERROR_DATA_OPERATION_FAIL = 0x12,
    RT_ERROR_INVALID_L2_INSTR_SIZE = 0x13,
    RT_ERROR_DEVICE_PROC_HANG_OUT = 0x14,
    RT_ERROR_DEVICE_POWER_UP_FAIL = 0x15,
    RT_ERROR_DEVICE_POWER_DOWN_FAIL = 0x16,
    RT_ERROR_FEATURE_NOT_SUPPROT = 0x17,
    RT_ERROR_KERNEL_DUPLICATE = 0x18,         // register same kernel repeatly
    RT_ERROR_MODEL_STREAM_EXE_FAILED = 0x91,  // the model stream failed
    RT_ERROR_MODEL_LOAD_FAILED = 0x94,        // the model stream failed
    RT_ERROR_END_OF_SEQUENCE = 0x95,          // end of sequence
    RT_ERROR_NO_STREAM_CB_REG = 0x96,         // no callback register info for stream
    RT_ERROR_DATA_DUMP_LOAD_FAILED = 0x97,    // data dump load info fail
    RT_ERROR_CALLBACK_THREAD_UNSUBSTRIBE = 0x98,    // callback thread unsubstribe
    RT_ERROR_RESERVED
} rtError_t;
/**
 * @ingroup dvrt_mem
 * @brief memory copy type
 */
typedef enum tagRtMemcpyKind {
    RT_MEMCPY_HOST_TO_HOST = 0,  // host to host
    RT_MEMCPY_HOST_TO_DEVICE,    // host to device
    RT_MEMCPY_DEVICE_TO_HOST,    // device to host
    RT_MEMCPY_DEVICE_TO_DEVICE,  // device to device, 1P && P2P
    RT_MEMCPY_MANAGED,           // managed memory
    RT_MEMCPY_ADDR_DEVICE_TO_DEVICE,
    RT_MEMCPY_HOST_TO_DEVICE_EX, // host  to device ex (only used for 8 bytes)
    RT_MEMCPY_DEVICE_TO_HOST_EX, // device to host ex
    RT_MEMCPY_RESERVED,
} rtMemcpyKind_t;

/**
 * @ingroup rt_kernel
 * @brief device binary type
 */
typedef struct tagRtDevBinary {
    uint32_t magic;    // magic number
    uint32_t version;  // version of binary
    const void *data;  // binary data
    uint64_t length;   // binary length
} rtDevBinary_t;

/**
 * @ingroup dvrt_base
 * @brief stream handle.
 */
typedef void *rtStream_t;
typedef void *rtModel_t;
typedef void (*rtCallback_t)(void *fnData);
typedef void *rtContext_t;

/**
 * @ingroup rt_kernel
 * @brief shared memory data control
 */
typedef struct tagRtSmData {
    uint64_t L2_mirror_addr;          // preload or swap source addr
    uint32_t L2_data_section_size;    // every data size
    uint8_t L2_preload;               // 1 - preload from mirrorAddr, 0 - no preload
    uint8_t modified;                 // 1 - data will be modified by kernel, 0 - no modified
    uint8_t priority;                 // data priority
    int8_t prev_L2_page_offset_base;  // remap source section offset
    uint8_t L2_page_offset_base;      // remap destination section offset
    uint8_t L2_load_to_ddr;           // 1 - need load out, 0 - no need
    uint8_t reserved[2];              // reserved
} rtSmData_t;

/**
 * @ingroup rt_kernel
 * @brief host memory input struct
 */
typedef struct rtHostInputInfo {
    uint32_t addrOffset;
    uint32_t dataOffset;
} rtHostInputInfo_t;

/**
 * @ingroup rt_kernel
 * @brief args struct
 */
typedef struct tagRtArgsEx {
    void *args;                     // args host mem addr
    rtHostInputInfo_t *hostInputInfoPtr;     // nullptr means no host mem input
    uint32_t argsSize;              // input + output + tiling addr size + tiling data size + host mem
    uint32_t tilingAddrOffset;      // tiling addr offset
    uint32_t tilingDataOffset;      // tiling data offset
    uint16_t hostInputInfoNum;      // hostInputInfo num
    uint8_t hasTiling;              // if has tiling: 0 means no tiling
    uint8_t isNoNeedH2DCopy;        // is no need host to device copy: 0 means need H2D copy,
                                    // others means doesn't need H2D copy.
    uint8_t reserved[4];
} rtArgsEx_t;

typedef struct tagRtTaskCfgInfo {
    uint8_t qos;
    uint8_t partId;
    uint8_t schemMode; // rtschemModeType_t 0:normal;1:batch;2:sync
    bool d2dCrossFlag;
    uint32_t blockDimOffset;
    uint8_t dumpflag;
    uint8_t neverTimeout;
    uint8_t res[2]; // res
    uint32_t localMemorySize;
} rtTaskCfgInfo_t;

/**
 * @ingroup rt_kernel
 * @brief shared memory description
 */
typedef struct tagRtSmCtrl {
    rtSmData_t data[8];  // data description
    uint64_t size;       // max page Num
    uint8_t remap[64];   /* just using for static remap mode, default:0xFF
                            array index: virtual l2 page id, array value: physic l2 page id */
    uint8_t l2_in_main;  // 0-DDR, 1-L2, default:0xFF
    uint8_t reserved[3];
} rtSmDesc_t;

typedef struct rtArgsSizeInfo {
    void *infoAddr; /* info : atomicIndex|input num input offset|size|size */
    uint32_t atomicIndex;
} rtArgsSizeInfo_t;

/**
 * @ingroup dvrt_base
 * @brief device mode.
 */
typedef enum tagRtDeviceMode {
    RT_DEVICE_MODE_SINGLE_DIE = 0,
    RT_DEVICE_MODE_MULTI_DIE,
    RT_DEVICE_MODE_RESERVED
} rtDeviceMode;

typedef enum tagRtDeviceStatus {
    RT_DEVICE_STATUS_NORMAL = 0,
    RT_DEVICE_STATUS_ABNORMAL,
    RT_DEVICE_STATUS_END = 0xFFFF
} rtDeviceStatus;

/**
 * @ingroup dvrt_base
 * @brief mc2.
 */
typedef enum rtKernelType {
    KERNEL_TYPE_CCE = 0,
    KERNEL_TYPE_FWK = 1,
    KERNEL_TYPE_AICPU = 2,
    KERNEL_TYPE_AICPU_CUSTOM = 4,
    KERNEL_TYPE_AICPU_KFC = 5,
    KERNEL_TYPE_HWTS = 10,
    KERNEL_TYPE_RESERVED = 99,
} rtKernelType_t;

typedef struct tagRtAicpuArgsEx {
    void *args; // args host mem addr
    rtHostInputInfo_t *hostInputInfoPtr; // nullptr means no host mem input
    rtHostInputInfo_t *kernelOffsetInfoPtr; // KernelOffsetInfo, it is different for CCE Kernel and fwk kernel
    uint32_t argsSize;
    uint16_t hostInputInfoNum; // hostInputInfo num
    uint16_t kernelOffsetInfoNum; // KernelOffsetInfo num
    uint16_t soNameAddrOffset; // just for CCE Kernel, default value is 0xffff for FWK kernel
    uint16_t kernelNameAddrOffset; // just for CCE Kernel, default value is 0xffff for FWK kernel
    bool isNoNeedH2DCopy; // is no need host to device copy: 0 means need H2D copy,
    // other means doesn't need H2D copy.
    uint8_t reserved[3];
} rtAicpuArgsEx_t;
typedef struct DrvMemHandle {
    int32_t id;
    uint32_t side;
    uint32_t devid;
    uint64_t pg_num;
} rtDrvMemHandle_t;

/**
* * if the type you input is not compatitable with the table below, then will return fail
* --------------------------------------------------------------------------------------------------------
* moduleType                |        infoType             |    value                    |   attention    |
* --------------------------------------------------------------------------------------------------------
* MODULE_TYPE_SYSTEM        |  INFO_TYPE_ENV              |   env type                  |                |
* MODULE_TYPE_SYSTEM        |  INFO_TYPE_VERSION          |   hardware_version          |                |
* MODULE_TYPE_SYSTEM        |  INFO_TYPE_MASTERID         |   masterId                  | used in host   |
* MODULE_TYPE_SYSTEM        |  INFO_TYPE_CORE_NUM         |   ts_num                    |                |
* MODULE_TYPE_SYSTEM        |  INFO_TYPE_SYS_COUNT        |   system count              |                |
* MODULE_TYPE_SYSTEM        |INFO_TYPE_MONOTONIC_RAW      |   MONOTONIC_RAW time        |                |
* --------------------------------------------------------------------------------------------------------
* MODULE_TYPE_AICPU         |  INFO_TYPE_CORE_NUM         |   ai cpu number             |                |
* MODULE_TYPE_AICPU         |  INFO_TYPE_OS_SCHED         |   ai cpu in os sched        | used in device |
* MODULE_TYPE_AICPU         |  INFO_TYPE_IN_USED          |   ai cpu in used            |                |
* MODULE_TYPE_AICPU         |  INFO_TYPE_ERROR_MAP        |   ai cpu error map          |                |
* MODULE_TYPE_AICPU         |  INFO_TYPE_ID               |   ai cpu id                 |                |
* MODULE_TYPE_AICPU         |  INFO_TYPE_OCCUPY           |   ai cpu occupy bitmap      |                |
* --------------------------------------------------------------------------------------------------------
* MODULE_TYPE_CCPU          |  INFO_TYPE_CORE_NUM         |   ctrl cpu number           |                |
* MODULE_TYPE_CCPU          |  INFO_TYPE_ID               |   ctrl cpu id               |                |
* MODULE_TYPE_CCPU          |  INFO_TYPE_IP               |   ctrl cpu ip               |                |
* MODULE_TYPE_CCPU          |  INFO_TYPE_ENDIAN           |   ctrl cpu ENDIAN           |                |
* MODULE_TYPE_CCPU          |  INFO_TYPE_OS_SCHED         |   ctrl cpu  in os sched     | used in device |
* --------------------------------------------------------------------------------------------------------
* MODULE_TYPE_DCPU          |  INFO_TYPE_CORE_NUM         |   data cpu number           | used in device |
* MODULE_TYPE_DCPU          |  INFO_TYPE_OS_SCHED         |   data cpu in os sched      | used in device |
* --------------------------------------------------------------------------------------------------------
* MODULE_TYPE_AICORE        |  INFO_TYPE_CORE_NUM         |   ai core number            |                |
* MODULE_TYPE_AICORE        |  INFO_TYPE_CORE_NUM_LEVEL   |   ai core number level      |                |
* MODULE_TYPE_AICORE        |  INFO_TYPE_IN_USED          |   ai core in used           |                |
* MODULE_TYPE_AICORE        |  INFO_TYPE_ERROR_MAP        |   ai core error map         |                |
* MODULE_TYPE_AICORE        |  INFO_TYPE_ID               |   ai core id                |                |
* MODULE_TYPE_AICORE        |  INFO_TYPE_FREQUE           |   ai core frequence         |                |
* MODULE_TYPE_AICORE        |  INFO_TYPE_FREQUE_LEVEL     |   ai core frequence level   |                |
* --------------------------------------------------------------------------------------------------------
* MODULE_TYPE_VECTOR_CORE   |   INFO_TYPE_CORE_NUM        | vector core number          |                |
* MODULE_TYPE_VECTOR_CORE   |   INFO_TYPE_FREQUE          | vector core frequence       |                |
* --------------------------------------------------------------------------------------------------------
* MODULE_TYPE_TSCPU         |  INFO_TYPE_CORE_NUM         |   ts cpu number             |                |
* MODULE_TYPE_TSCPU         |  INFO_TYPE_OS_SCHED         |   ts cpu in os sched        | used in device |
* MODULE_TYPE_TSCPU         |  INFO_TYPE_FFTS_TYPE        |   ts cpu ffts type          | used in device |
* --------------------------------------------------------------------------------------------------------
* MODULE_TYPE_PCIE          |  INFO_TYPE_ID               |   pcie bdf                  | used in host   |
* --------------------------------------------------------------------------------------------------------
* @param [in] devId  Device ID
* @param [in] moduleType  See enum DEV_MODULE_TYPE
* @param [in] infoType  See enum DEV_INFO_TYPE
* @param [out] *value  device info
* @return   0 for success, others for fail
*/
typedef enum tagRtDeviceModuleType {
    RT_MODULE_TYPE_SYSTEM = 0,  /**< system info*/
    RT_MODULE_TYPE_AICPU,       /** < aicpu info*/
    RT_MODULE_TYPE_CCPU,        /**< ccpu_info*/
    RT_MODULE_TYPE_DCPU,        /**< dcpu info*/
    RT_MODULE_TYPE_AICORE,      /**< AI CORE info*/
    RT_MODULE_TYPE_TSCPU,       /**< tscpu info*/
    RT_MODULE_TYPE_PCIE,        /**< PCIE info*/
    RT_MODULE_TYPE_VECTOR_CORE, /**< VECTOR CORE info*/
    RT_MODULE_TYPE_HOST_AICPU,   /**< HOST AICPU info*/
    RT_MODULE_TYPE_QOS,         /**<qos info> */
    RT_MODULE_TYPE_MEMORY      /**<memory info*/
} rtDeviceModuleType_t;

constexpr int rtMemoryHbm = 0x2U;      // HBM memory on device
constexpr int rtDevBinaryMagicElf = 0x43554245U;
constexpr int rtDevBinaryMagicElfAivec = 0x41415246U;
constexpr int rtDevBinaryMagicElfAicube = 0x41494343U;

typedef enum tagRtLimitType {
    RT_LIMIT_TYPE_LOW_POWER_TIMEOUT = 0,  // timeout for power down , ms
    RT_LIMIT_TYPE_SIMT_WARP_STACK_SIZE = 1,
    RT_LIMIT_TYPE_SIMT_DVG_WARP_STACK_SIZE = 2,
    RT_LIMIT_TYPE_STACK_SIZE = 3,  // max stack size for each core, bytes
    RT_LIMIT_TYPE_RESERVED,
} rtLimitType_t;


/**
 * @ingroup dvrt_dev
 * @brief set target device for current thread
 * @param [int] devId   the device id
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtSetDevice(int32_t devId);

/**
 * @ingroup dvrt_dev
 * @brief get chipType
 * @return RT_ERROR_NONE for ok
 */
RTS_API rtError_t rtGetSocVersion(char *ver, const uint32_t maxLen);

/**
 * @ingroup dvrt_mem
 * @brief free device memory
 * @param [in|out] devPtr   memory pointer
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtFree(void *devPtr);

/**
 * @ingroup dvrt_mem
 * @brief alloc device memory
 * @param [in|out] devPtr   memory pointer
 * @param [in] size   memory size
 * @param [in] type   memory type
 * @param [in] moduleid alloc memory module id
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtMalloc(void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId);

/**
 * @ingroup dvrt_mem
 * @brief This command is used to map an allocation handle to a reserved virtual address range.
 * @attention Only support ONLINE scene.
 * @param [in] devPtr Address where memory will be mapped.
 * @param [in] size Size of the memory mapping.
 * @param [in] offset Currently unused, must be zero.
 * @param [in] handle Value of handle which was returned previously by halMemCreate.
 * @param [in] flag Currently unused, must be zero.
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 * @return RT_ERROR_DRV_ERR for driver error
 */
RTS_API rtError_t rtMapMem(void* devPtr, size_t size, size_t offset, rtDrvMemHandle_t* handle, uint64_t flags);

/**
 * @ingroup dvrt_mem
 * @brief This command is used to unmap the backing memory of a given address range.
 * @attention Only support ONLINE scene.
 * @param [in] devPtr Starting address for the virtual address range to unmap.
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 * @return RT_ERROR_DRV_ERR for driver error
 */
RTS_API rtError_t rtUnmapMem(void* devPtr);

/**
 * @ingroup rt_kernel
 * @brief register device binary
 * @param [in] bin   device binary description
 * @param [out] hdl   device binary handle
 * @return RT_ERROR_NONE for ok
 * @note:if this interface is changed, pls notify the compiler changing at the same time.
 */
RTS_API rtError_t rtDevBinaryRegister(const rtDevBinary_t *bin, void **hdl);

/**
 * @ingroup dvrt_mem
 * @brief set memory with uint32_t value
 * @param [in] devPtr
 * @param [in] Max length of destination address memory
 * @param [in] val
 * @param [in] cnt byte num
 * @return RT_ERROR_NONE for ok, errno for failed
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtMemset(void *devPtr, uint64_t destMax, uint32_t val, uint64_t cnt);

/**
 * @ingroup dvrt_mem
 * @brief synchronized memcpy
 * @param [in] dst   destination address pointer
 * @param [in] Max length of destination address memory
 * @param [in] src   source address pointer
 * @param [in] cnt   the number of byte to copy
 * @param [in] kind   memcpy type
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtMemcpy(void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind);

/**
 * @ingroup rt_kernel
 * @brief register device binary with all kernel
 * @param [in] bin   device binary description
 * @param [out] hdl   device binary handle
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtRegisterAllKernel(const rtDevBinary_t *bin, void **hdl);

/**
 * @ingroup rt_kernel
 * @brief unregister device binary
 * @param [in] hdl   device binary handle
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtDevBinaryUnRegister(void *hdl);

/**
 * @ingroup rt_kernel
 * @brief register device function
 * @param [in] binHandle   device binary handle
 * @param [in] stubFunc   stub function
 * @param [in] stubName   stub function name
 * @param [in] kernelInfoExt   kernel Info extension. device function description or tiling key,
 *                             depending static shape or dynmaic shape.
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtFunctionRegister(void *binHandle, const void *stubFunc, const char *stubName,
                                     const void *kernelInfoExt, uint32_t funcMode);

/**
 * @ingroup rt_kernel
 * @brief launch kernel to device
 * @param [in] stubFunc   stub function
 * @param [in] blockDim   block dimentions
 * @param [in] args   argments address for kernel function
 * @param [in] argsSize   argements size
 * @param [in] smDesc   shared memory description
 * @param [in] stm   associated stream
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtKernelLaunch(const void *stubFunc, uint32_t blockDim, void *args,
                                 uint32_t argsSize, rtSmDesc_t *smDesc, rtStream_t stm);

/**
 * @ingroup rt_kernel
 * @brief launch kernel with handle to device
 * @param [in] hdl             program
 * @param [in] tilingKey       tilingKey
 * @param [in] blockDim        block dimentions
 * @param [in] argsInfo        argments address for kernel function
 * @param [in] smDesc          shared memory description
 * @param [in] stm             associated stream
 * @param [in] cfgInfo      task config
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtKernelLaunchWithHandleV2(void *hdl, const uint64_t tilingKey, uint32_t blockDim,
                                             rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm,
                                             const rtTaskCfgInfo_t *cfgInfo);

RTS_API rtError_t rtAicpuKernelLaunchExWithArgs(const uint32_t kernelType, const char *const opName,
    const uint32_t blockDim, const rtAicpuArgsEx_t *argsInfo, rtSmDesc_t *const smDesc, const rtStream_t stm,
    const uint32_t flags);

/**
 * @ingroup rtKernelLaunchWithFlag
 * @brief launch kernel to device
 * @param [in] stubFunc   stub function
 * @param [in] blockDim   block dimentions
 * @param [in] argsInfo   argments address for kernel function
 * @param [in] smDesc     shared memory description
 * @param [in] stm        associated stream
 * @param [in] flags      dump flag
 * @param [in] cfgInfo      task config info
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtKernelLaunchWithFlagV2(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                                           rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags,
                                           const rtTaskCfgInfo_t *cfgInfo);

/**
* @ingroup rt_kernel
* @brief set input argments size for exception
* @param [in] sizeInfo argments size info
* @return RT_ERROR_NONE for ok
* @return RT_ERROR_INVALID_VALUE for error input
*/
RTS_API rtError_t rtSetExceptionExtInfo(const rtArgsSizeInfo_t * const sizeInfo);


RTS_API rtError_t rtMallocHost(void **hostPtr, uint64_t size, const uint16_t moduleId);
RTS_API rtError_t rtFreeHost(void *hostPtr);
RTS_API rtError_t rtGetDevice(int32_t* devId);
RTS_API rtError_t rtGetDeviceInfo(uint32_t deviceId, int32_t moduleType, int32_t infoType, int64_t *val);
RTS_API rtError_t rtSetDeviceV2(int32_t devId, rtDeviceMode deviceMode);
RTS_API rtError_t rtDeviceReset(int32_t devId);
RTS_API rtError_t rtDeviceResetEx(int32_t devId);
RTS_API rtError_t rtSetDeviceEx(int32_t devId);
RTS_API rtError_t rtCtxCreate(void **createCtx, uint32_t flags, int32_t devId);
RTS_API rtError_t rtCtxCreateV2(void **createCtx, uint32_t flags, int32_t devId, rtDeviceMode deviceMode);
RTS_API rtError_t rtCtxCreateEx(void **ctx, uint32_t flags, int32_t devId);
RTS_API rtError_t rtDeviceStatusQuery(const uint32_t devId, rtDeviceStatus *deviceStatus);
RTS_API rtError_t rtStreamCreate(rtStream_t *stream, int32_t priority);
RTS_API rtError_t rtStreamDestroy(rtStream_t stream);
RTS_API rtError_t rtStreamSynchronizeWithTimeout(rtStream_t stream, int32_t timeout);
RTS_API rtError_t rtGetC2cCtrlAddr(uint64_t *addr, uint32_t *fftsLen);
RTS_API rtError_t rtProfSetProSwitch(void *data, uint32_t len);
RTS_API rtError_t rtGetAiCoreCount(uint32_t *aiCoreCnt);
RTS_API rtError_t rtKernelLaunchWithHandle(void *hdl, const uint64_t tilingKey, uint32_t blockDim, rtArgsEx_t *argsInfo,
                                           rtSmDesc_t *smDesc, rtStream_t stm, const void* kernelInfo);
RTS_API rtError_t rtGetL2CacheOffset(uint32_t deviceId, uint64_t *offset);
/**
 * @ingroup dvrt_mem
 * @brief asynchronized memcpy
 * @param [in] dst   destination address pointer
 * @param [in] Max length of destination address memory
 * @param [in] src   source address pointer
 * @param [in] cnt   the number of byte to copy
 * @param [in] kind   memcpy type
 * @param [in] stm   asynchronized task stream
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtMemcpyAsync(void *dst, uint64_t destMax, const void *src, uint64_t cnt,
                                rtMemcpyKind_t kind, rtStream_t stm);

/**
 * @ingroup dvrt_stream
 * @brief wait stream to be complete
 * @param [in] stm   stream to wait
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtStreamSynchronize(rtStream_t stm);

/**
 * @ingroup dvrt_dev
 * @brief get total device number.
 * @param [in|out] cnt the device number
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 */
RTS_API rtError_t rtGetDeviceCount(int32_t *cnt);

/**
 * @ingroup dvrt_mem
 * @brief make memory shared interprocess and assigned a name
 * @param [in] ptr    device memory address pointer
 * @param [in] name   identification name
 * @param [in] byteCount   identification byteCount
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 * @return RT_ERROR_DRV_ERR for driver error
 */
RTS_API rtError_t rtIpcSetMemoryName(const void *ptr, uint64_t byteCount, char *name, uint32_t len);

/**
 * @ingroup dvrt_mem
 * @brief destroy a interprocess shared memory
 * @param [in] name   identification name
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 * @return RT_ERROR_DRV_ERR for driver error
 */
RTS_API rtError_t rtIpcDestroyMemoryName(const char *name);


/**
 * @ingroup dvrt_mem
 * @brief open a interprocess shared memory
 * @param [in|out] ptr    device memory address pointer
 * @param [in] name   identification name
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 * @return RT_ERROR_DRV_ERR for driver error
 */
RTS_API rtError_t rtIpcOpenMemory(void **ptr, const char *name);

/**
 * @ingroup dvrt_mem
 * @brief close a interprocess shared memory
 * @param [in] ptr    device memory address pointer
 * @param [in] name   identification name
 * @return RT_ERROR_NONE for ok
 * @return RT_ERROR_INVALID_VALUE for error input
 * @return RT_ERROR_DRV_ERR for driver error
 */
RTS_API rtError_t rtIpcCloseMemory(const void *ptr);
RTS_API rtError_t rtModelExecute(rtModel_t mdl, rtStream_t stm, uint32_t flag);
RTS_API rtError_t rtCallbackLaunch(rtCallback_t callBackFunc, void *fnData, rtStream_t stm, bool isBlock);
RTS_API rtError_t rtSubscribeReport(uint64_t threadId, rtStream_t stm);
RTS_API rtError_t rtProcessReport(int32_t timeout);
RTS_API rtError_t rtUnSubscribeReport(uint64_t threadId, rtStream_t stm);
RTS_API rtError_t rtModelBindStream(rtModel_t mdl, rtStream_t stm, uint32_t flag);

/**
 * @ingroup rt_context
 * @brief get context overflowAddr
 * @param [out] overflowAddr current ctx's overflowAddr to be get
 * @return RT_ERROR_NONE for ok, errno for failed
 */
RTS_API rtError_t rtCtxGetOverflowAddr(void **overflowAddr);

RTS_API rtError_t rtDeviceSetLimit(int32_t devId, rtLimitType_t type, uint32_t val);

RTS_API rtError_t rtRegisterFuncSymbol(void *binHandle, const void *symbol, const char *kernelName, void *reserve);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // __RUNTIME_H__
