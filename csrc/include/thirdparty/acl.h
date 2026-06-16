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

// 这个文件本身需要与被劫持的对象一样

#ifndef __ASCENDCL_H__
#define __ASCENDCL_H__

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum aclrtMemMallocPolicy {
    ACL_MEM_MALLOC_HUGE_FIRST,
    ACL_MEM_MALLOC_HUGE_ONLY,
    ACL_MEM_MALLOC_NORMAL_ONLY,
    ACL_MEM_MALLOC_HUGE_FIRST_P2P,
    ACL_MEM_MALLOC_HUGE_ONLY_P2P,
    ACL_MEM_MALLOC_NORMAL_ONLY_P2P,
    ACL_MEM_TYPE_LOW_BAND_WIDTH   = 0x0100,
    ACL_MEM_TYPE_HIGH_BAND_WIDTH  = 0x1000,
} aclrtMemMallocPolicy;

typedef enum aclrtMemcpyKind {
    ACL_MEMCPY_HOST_TO_HOST,
    ACL_MEMCPY_HOST_TO_DEVICE,
    ACL_MEMCPY_DEVICE_TO_HOST,
    ACL_MEMCPY_DEVICE_TO_DEVICE,
} aclrtMemcpyKind;

typedef enum aclrtLaunchKernelAttrId {
    ACL_RT_LAUNCH_KERNEL_ATTR_SCHEM_MODE = 1,  // 调度模式
} aclrtLaunchKernelAttrId;

typedef union aclrtLaunchKernelAttrValue {
    uint8_t schemMode;
    uint32_t localMemorySize;
    uint32_t rsv[4];
} aclrtLaunchKernelAttrValue;

typedef struct aclrtLaunchKernelAttr {
    aclrtLaunchKernelAttrId id;
    aclrtLaunchKernelAttrValue value;
} aclrtLaunchKernelAttr;

typedef struct aclrtLaunchKernelCfg {
    aclrtLaunchKernelAttr *attrs;
    size_t numAttrs;
} aclrtLaunchKernelCfg;

typedef int aclError;
typedef void* aclrtStream;
typedef void* aclrtEvent;

typedef void* aclrtDrvMemHandle;

typedef void* aclrtBinHandle;
typedef void* aclrtFuncHandle;
typedef void* aclrtParamHandle;
typedef void* aclrtBinary;
typedef void* aclrtArgsHandle;
typedef void *aclmdlRI;
typedef void *aclrtContext;
typedef void (*aclrtCallback)(void *userData);

typedef enum aclrtBinaryLoadOptionType {
    ACL_RT_BINARY_LOAD_OPT_LAZY_LOAD = 1,       // 指定解析算子二进制、注册算子后，是否加载算子到Device侧
    ACL_RT_BINARY_LOAD_OPT_LAZY_MAGIC = 2,
    ACL_RT_BINARY_LOAD_OPT_MAGIC = 2,           // 标识算子类型的魔术数字
    ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE = 3, // AI CPU算子注册模式
    NONE = 10000
} aclrtBinaryLoadOptionType;

typedef union aclrtBinaryLoadOptionValue {
    uint32_t isLazyLoad;
    uint32_t magic;
    int32_t cpuKernelMode;
    uint32_t rsv[4];
} aclrtBinaryLoadOptionValue;

typedef struct {
    aclrtBinaryLoadOptionType type;
    aclrtBinaryLoadOptionValue value;
} aclrtBinaryLoadOption;

typedef struct aclrtBinaryLoadOptions {
    aclrtBinaryLoadOption *options;
    size_t numOpt;
} aclrtBinaryLoadOptions;

typedef enum aclrtDeviceStatus {
    ACL_RT_DEVICE_STATUS_NORMAL = 0,     // Device正常可用
    ACL_RT_DEVICE_STATUS_ABNORMAL,       // Device异常
    ACL_RT_DEVICE_STATUS_END = 0xFFFF,   // 预留值
} aclrtDeviceStatus;

typedef enum {
    ACL_DEV_ATTR_AICPU_CORE_NUM  = 1,    // AI CPU数量
    ACL_DEV_ATTR_AICORE_CORE_NUM = 101,  // AI Core数量
    ACL_DEV_ATTR_VECTOR_CORE_NUM = 201,  // Vector Core数量
} aclrtDevAttr;
typedef enum {
    ACL_RT_MEM_ATTR_RSV = 0U,   // 预留值
    ACL_RT_MEM_ATTR_MODULE_ID,  // 表示模块ID
    ACL_RT_MEM_ATTR_DEVICE_ID,  // 表示Device ID
} aclrtMallocAttrType;
typedef union {
    uint16_t moduleId;
    uint32_t deviceId;
    uint8_t rsv[8];
} aclrtMallocAttrValue;
typedef struct {
    aclrtMallocAttrType attr;
    aclrtMallocAttrValue value;
} aclrtMallocAttribute;
typedef struct {
    aclrtMallocAttribute* attrs;
    size_t numAttrs;
} aclrtMallocConfig;

typedef struct {
    uint32_t addrOffset;
    uint32_t dataOffset;
} aclrtPlaceHolderInfo;

typedef enum aclrtCmoType {
    ACL_RT_CMO_TYPE_PREFETCH = 0,     // 内存预取，从内存预取到Cache
    ACL_RT_CMO_TYPE_WRITEBACK,        // 把Cache中的数据刷新到内存中，并在Cache中保留副本
    ACL_RT_CMO_TYPE_INVALID,          // 丢弃Cache中的数据
    ACL_RT_CMO_TYPE_FLUSH,            // 把Cache中的数据刷新到内存中，不保留Cache中的副本
} aclrtCmoType;

typedef enum {
    ACL_FUNC_ATTR_KERNEL_TYPE  = 1,
} aclrtFuncAttribute;

typedef enum {
    ACL_KERNEL_TYPE_AICORE  = 0,
    ACL_KERNEL_TYPE_CUBE  = 1,
    ACL_KERNEL_TYPE_VECTOR  = 2,
    ACL_KERNEL_TYPE_MIX = 3,
    ACL_KERNEL_TYPE_AICPU  = 100,
} aclrtKernelType;

typedef enum aclrtCallbackBlockType {
    ACL_CALLBACK_NO_BLOCK,  //非阻塞
    ACL_CALLBACK_BLOCK,  //阻塞
} aclrtCallbackBlockType;

static const int ACL_ERROR_NONE = 0;
static const int ACL_SUCCESS = 0;
static const int ACL_ERROR_BAD_ALLOC = 200000;
static const int ACL_ERROR_INTERNAL_ERROR = 500000;
static const int ACL_ERROR_RT_STREAM_SYNC_TIMEOUT = 507046;

aclError aclrtSetDevice(int32_t deviceId);
aclError aclrtGetUserDevIdByLogicDevId(const int32_t logicDevId, int32_t *const userDevid);
aclError aclrtCreateStream(aclrtStream *stream);
aclError aclrtDestroyStream(aclrtStream stream);
aclError aclrtResetDevice(int32_t devId);
aclError aclrtMallocHost(void **hostPtr, size_t size);
aclError aclrtFreeHost(void *hostPtr);
aclError aclrtMalloc(void **devPtr, size_t size, aclrtMemMallocPolicy policy);
aclError aclrtMallocWithCfgImpl(void **devPtr, size_t size, aclrtMemMallocPolicy policy, aclrtMallocConfig *cfg);
aclError aclrtFree(void *devPtr);
aclError aclrtMemcpy(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind);
aclError aclrtBinaryLoadFromFile(const char* binPath, aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle);
aclError aclrtLaunchKernelWithConfig(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream,
                                     aclrtLaunchKernelCfg *cfg, aclrtArgsHandle argsHandle, void *reserve);
aclError aclrtKernelArgsAppend(aclrtArgsHandle argsHandle, void *param, size_t paramSize, aclrtParamHandle *paramHandle);
aclError aclrtKernelArgsFinalize(aclrtArgsHandle argsHandle);
aclError aclrtBinaryGetFunction(const aclrtBinHandle binHandle, const char *kernelName, aclrtFuncHandle *funcHandle);
aclError aclrtBinaryGetFunctionByEntry(aclrtBinHandle binHandle, uint64_t funcEntry, aclrtFuncHandle *funcHandle);
aclError aclrtRegisterCpuFunc(const aclrtBinHandle binHandle, const char *funcName, const char *kernelName, aclrtFuncHandle *funcHandle);
aclError aclrtKernelArgsInit(aclrtFuncHandle funcHandle, aclrtArgsHandle *argsHandle);
aclError aclrtBinaryUnLoad(aclrtBinHandle binHandle);
aclError aclrtSynchronizeStream(aclrtStream stream);

aclError aclrtSetOpExecuteTimeOut(uint32_t timeout);
aclError aclrtCreateEvent(aclrtEvent *event);
aclError aclrtStreamWaitEvent(aclrtStream stream, aclrtEvent event);
aclError aclrtRecordEvent(aclrtEvent event, aclrtStream stream);
aclError aclrtResetEvent(aclrtEvent event, aclrtStream stream);
aclError aclrtDestroyEvent(aclrtEvent event);
aclError aclrtSynchronizeStreamWithTimeoutImpl(aclrtStream stream, int32_t timeout);
aclError aclrtSynchronizeStreamImpl(aclrtStream stream);
aclError aclrtStreamAbortImpl(aclrtStream stream);
aclError aclrtMemcpyAsyncImpl(void *dst, size_t destMax, const void *src, size_t count,
                              aclrtMemcpyKind kind, aclrtStream stream);
aclError aclrtMallocHostImpl(void **hostPtr, size_t size);
aclError aclrtMallocHostWithCfgImpl(void **hostPtr, size_t size, aclrtMallocConfig *cfg);
aclError aclrtFreeHostImpl(void *hostPtr);
aclError aclrtCtxGetCurrentDefaultStreamImpl(aclrtStream *stream);
aclError aclrtSetDeviceImpl(int32_t deviceId);
aclError aclrtGetDeviceImpl(int32_t *deviceId);
aclError aclrtResetDeviceImpl(int32_t deviceId);
aclError aclrtResetDeviceForceImpl(int32_t deviceId);

aclError aclrtMallocImpl(void **devPtr, size_t size, aclrtMemMallocPolicy policy);

aclError aclrtMallocCachedImpl(void **devPtr, size_t size, aclrtMemMallocPolicy policy);

aclError aclrtFreeImpl(void *devPtr);

aclError aclrtMemsetImpl(void *devPtr, size_t maxCount, int32_t value, size_t count);

aclError aclrtMemsetAsyncImpl(void *devPtr, size_t maxCount, int32_t value, size_t count, aclrtStream stream);

aclError aclrtMemcpyImpl(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind);

aclError aclrtMemcpyAsyncImpl(void *dst, size_t destMax, const void *src, size_t count,
                              aclrtMemcpyKind kind, aclrtStream stream);

aclError aclrtMemcpy2dImpl(void *dst, size_t dpitch, const void *src, size_t spitch,
                           size_t width, size_t height, aclrtMemcpyKind kind);

aclError aclrtMemcpy2dAsyncImpl(void *dst, size_t dpitch, const void *src, size_t spitch,
                                size_t width, size_t height, aclrtMemcpyKind kind, aclrtStream stream);

aclError aclrtMapMemImpl(void *virPtr, size_t size, size_t offset, aclrtDrvMemHandle handle, uint64_t flags);
aclError aclrtUnmapMemImpl(void *virPtr);

aclError aclrtIpcMemGetExportKeyImpl(void *devPtr, size_t size, char *key, size_t len, uint64_t flag);
aclError aclrtIpcMemImportByKeyImpl(void **devPtr, const char *key, uint64_t flag);
aclError aclrtIpcMemCloseImpl(const char *key);

aclError aclrtBinaryLoadFromFileImpl(const char* binPath, aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle);

aclrtBinary aclrtCreateBinaryImpl(const void *data, size_t dataLen);
aclError aclrtBinaryLoadImpl(const aclrtBinary binary, aclrtBinHandle *binHandle);
aclError aclrtBinaryLoadFromDataImpl(const void *data, size_t length, const aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle);

aclError aclrtBinaryUnLoadImpl(aclrtBinHandle binHandle);
aclError aclrtDestroyBinaryImpl(aclrtBinary binary);

aclError aclrtBinaryGetFunctionImpl(const aclrtBinHandle binHandle, const char *kernelName, aclrtFuncHandle *funcHandle);
aclError aclrtBinaryGetFunctionByEntryImpl(aclrtBinHandle binHandle, uint64_t funcEntry, aclrtFuncHandle *funcHandle);
aclError aclrtRegisterCpuFuncImpl(const aclrtBinHandle binHandle, const char *funcName, const char *kernelName, aclrtFuncHandle *funcHandle);
aclError aclrtKernelArgsInitImpl(aclrtFuncHandle funcHandle, aclrtArgsHandle *argsHandle);
aclError aclrtKernelArgsInitByUserMemImpl(aclrtFuncHandle funcHandle, aclrtArgsHandle argsHandle, void *userHostMem, size_t actualArgsSize);
aclError aclrtKernelArgsGetMemSizeImpl(aclrtFuncHandle funcHandle, size_t userArgsSize, size_t *actualArgsSize);
aclError aclrtKernelArgsGetHandleMemSizeImpl(aclrtFuncHandle funcHandle, size_t *memSize);
aclError aclrtKernelArgsAppendImpl(aclrtArgsHandle argsHandle, void *param, size_t paramSize, aclrtParamHandle *paramHandle);
aclError aclrtKernelArgsAppendPlaceHolderImpl(aclrtArgsHandle argsHandle, aclrtParamHandle *paramHandle);
aclError aclrtKernelArgsGetPlaceHolderBufferImpl(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle, size_t dataSize, void **bufferAddr);
aclError aclrtKernelArgsParaUpdateImpl(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle, void *param, size_t paramSize);
aclError aclrtKernelArgsFinalizeImpl(aclrtArgsHandle argsHandle);
aclError aclrtLaunchKernelWithConfigImpl(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream, aclrtLaunchKernelCfg *cfg, aclrtArgsHandle argsHandle, void *reserve);
aclError aclrtLaunchKernelImpl(aclrtFuncHandle funcHandle, uint32_t blockDim, const void *argsData, size_t argsSize, aclrtStream stream);
aclError aclrtLaunchKernelV2Impl(aclrtFuncHandle funcHandle, uint32_t numBlocks, const void *argsData, size_t argsSize, aclrtLaunchKernelCfg *cfg, aclrtStream stream);
aclError aclrtLaunchKernelWithHostArgsImpl(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream,
    aclrtLaunchKernelCfg *cfg, void *hostArgs, size_t argsSize, aclrtPlaceHolderInfo *placeHolderArray, size_t placeHolderNum);
aclError aclrtGetFunctionAddrImpl(aclrtFuncHandle funcHandle, void **aicAddr, void **aivAddr);
aclError aclrtCreateContextImpl(aclrtContext *context, int32_t deviceId);
aclError aclrtQueryDeviceStatusImpl(int32_t deviceId, aclrtDeviceStatus *deviceStatus);
aclError aclrtGetDeviceCountImpl(uint32_t *count);
aclError aclrtGetDeviceInfoImpl(uint32_t deviceId, aclrtDevAttr attr, int64_t *value);
const char *aclrtGetSocNameImpl();
typedef enum {
    ACL_MODEL_RI_CAPTURE_MODE_GLOBAL = 0,   // 全局禁止，所有线程都不可以调用非安全函数
    ACL_MODEL_RI_CAPTURE_MODE_THREAD_LOCAL, // 当前线程禁止调用非安全函数
    ACL_MODEL_RI_CAPTURE_MODE_RELAXED,      // 全局不禁止，所有线程都可以调用非安全函数
} aclmdlRICaptureMode;

aclError aclmdlRICaptureBeginImpl(aclrtStream stream, aclmdlRICaptureMode mode);
aclError aclmdlRICaptureEndImpl(aclrtStream stream, aclmdlRI *modelRI);
aclError aclmdlRIBindStreamImpl(aclmdlRI modelRI, aclrtStream stream, uint32_t flag);
aclError aclmdlRIUnbindStreamImpl(aclmdlRI modelRI, aclrtStream stream);
aclError aclrtGetFunctionAttributeImpl(aclrtFuncHandle funcHandle, aclrtFuncAttribute attr, int64_t *value);
aclError aclrtLaunchCallbackImpl(aclrtCallback fn, void *userData, aclrtCallbackBlockType blockType, aclrtStream stream);
aclError aclrtProcessReportImpl(int32_t timeout);
aclError aclrtSubscribeReportImpl(uint64_t threadId, aclrtStream stream);
aclError aclrtGetFuncBySymbolImpl(const void *symbol, aclrtFuncHandle *funcHandle);
#ifdef __cplusplus
} // extern "C"
#endif

#endif // __ASCENDCL_H__
