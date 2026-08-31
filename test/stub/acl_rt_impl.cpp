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


#include "acl.h"
#include <cstring>
#include <malloc.h>
#include <algorithm>

using namespace std;

aclError aclrtMallocHostImpl(void **hostPtr, size_t size)
{
    return ACL_SUCCESS;
}

aclError aclrtFreeHostImpl(void *hostPtr)
{
    return ACL_SUCCESS;
}

aclError aclrtMemsetImpl(void *devPtr, size_t maxCount, int32_t value, size_t count)
{
    return ACL_SUCCESS;
}

aclError aclrtMemcpyAsyncImpl(void *dst, size_t destMax, const void *src,
                              size_t count, aclrtMemcpyKind kind, aclrtStream stream)
{
    return ACL_SUCCESS;
}

aclError aclrtBinaryLoadFromFileImpl(const char* binPath, aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle)
{
    return ACL_SUCCESS;
}

aclError aclrtBinaryLoadFromDataImpl(const void *data, size_t length,
                                     const aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle)
{
    return ACL_SUCCESS;
}

aclrtBinary aclrtCreateBinaryImpl(const void *data, size_t dataLen)
{
    static uint8_t anyData{};
    return aclrtBinary(&anyData);
}

aclError aclrtBinaryLoadImpl(const aclrtBinary binary, aclrtBinHandle *binHandle)
{
    return ACL_SUCCESS;
}

aclError aclrtBinaryUnLoadImpl(aclrtBinHandle binHandle)
{
    return ACL_SUCCESS;
}

aclError aclrtDestroyBinaryImpl(aclrtBinary binary)
{
    return ACL_SUCCESS;
}

aclError aclrtBinaryGetFunctionImpl(const aclrtBinHandle binHandle, const char *kernelName, aclrtFuncHandle *funcHandle)
{
    return ACL_SUCCESS;
}

aclError aclrtBinaryGetFunctionByEntryImpl(aclrtBinHandle binHandle, uint64_t funcEntry, aclrtFuncHandle *funcHandle)
{
    return ACL_SUCCESS;
}

aclError aclrtKernelArgsInitImpl(aclrtFuncHandle funcHandle, aclrtArgsHandle *argsHandle)
{
    return ACL_SUCCESS;
}

aclError aclrtKernelArgsInitByUserMemImpl(aclrtFuncHandle funcHandle, aclrtArgsHandle argsHandle, void *userHostMem, size_t actualArgsSize)
{
    return ACL_SUCCESS;
}

aclError aclrtKernelArgsGetMemSizeImpl(aclrtFuncHandle funcHandle, size_t userArgsSize, size_t *actualArgsSize)
{
    return ACL_SUCCESS;
}

aclError aclrtKernelArgsGetHandleMemSizeImpl(aclrtFuncHandle funcHandle, size_t *memSize)
{
    return ACL_SUCCESS;
}

aclError aclrtKernelArgsAppendImpl(aclrtArgsHandle argsHandle, void *param, size_t paramSize, aclrtParamHandle *paramHandle)
{
    return ACL_SUCCESS;
}

aclError aclrtKernelArgsAppendPlaceHolderImpl(aclrtArgsHandle argsHandle, aclrtParamHandle *paramHandle)
{
    return ACL_SUCCESS;
}

aclError aclrtKernelArgsGetPlaceHolderBufferImpl(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle, size_t dataSize, void **bufferAddr)
{
    return ACL_SUCCESS;
}

aclError aclrtKernelArgsParaUpdateImpl(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle, void *param, size_t paramSize)
{
    return ACL_SUCCESS;
}

aclError aclrtKernelArgsFinalizeImpl(aclrtArgsHandle argsHandle)
{
    return ACL_SUCCESS;
}

aclError aclrtLaunchKernelWithConfigImpl(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream, aclrtLaunchKernelCfg *cfg, aclrtArgsHandle argsHandle, void *reserve)
{
    return ACL_SUCCESS;
}

aclError aclrtLaunchKernelWithHostArgsImpl(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream, aclrtLaunchKernelCfg *cfg,
    void *hostArgs, size_t argsSize, aclrtPlaceHolderInfo *placeHolderArray, size_t placeHolderNum)
{
    return ACL_SUCCESS;
}

aclError aclrtLaunchKernelImpl(aclrtFuncHandle funcHandle, uint32_t blockDim, const void *argsData, size_t argsSize, aclrtStream stream)
{
    return ACL_SUCCESS;
}

aclError aclrtLaunchSIMTKernelWithHostArgsImpl(void *func, dim3 gridDim, dim3 blockDim, size_t dynUbufSize,
    aclrtStream stream, aclrtLaunchKernelCfg *cfg, void *hostArgs, size_t argsSize,
    aclrtPlaceHolderInfo *placeHolderArray, size_t placeHolderNum) {
    return ACL_SUCCESS;
}

aclError aclrtLaunchSIMTKernelWithArgsArrayImpl(void *func, dim3 gridDim, dim3 blockDim, size_t dynUbufSize,
    aclrtStream stream, aclrtLaunchKernelCfg *cfg, void **args) {
    return ACL_SUCCESS;
}

aclError aclrtLaunchKernelV2Impl(aclrtFuncHandle funcHandle, uint32_t numBlocks, const void *argsData, size_t argsSize, aclrtLaunchKernelCfg *cfg, aclrtStream stream)
{
    return ACL_SUCCESS;
}

aclError aclrtResetDeviceForceImpl(int32_t deviceId)
{
    return ACL_SUCCESS;
}

aclError aclrtGetDeviceImpl(int32_t *deviceId)
{
    deviceId = 0;
    return ACL_SUCCESS;
}

aclError aclrtGetFunctionAddrImpl(aclrtFuncHandle funcHandle, void **aicAddr, void **aivAddr)
{
    return ACL_SUCCESS;
}

aclError aclrtCreateContextImpl(aclrtContext *context, int32_t deviceId)
{
    return ACL_SUCCESS;
}

aclError aclrtQueryDeviceStatusImpl(int32_t deviceId, aclrtDeviceStatus *deviceStatus)
{
    return ACL_SUCCESS;
}

aclError aclrtGetDeviceCountImpl(uint32_t *count)
{
    return ACL_SUCCESS;
}

aclError aclrtGetDeviceInfoImpl(uint32_t deviceId, aclrtDevAttr attr, int64_t *value)
{
    return ACL_SUCCESS;
}

aclError aclmdlRICaptureBeginImpl(aclrtStream stream, aclmdlRICaptureMode mode)
{
    return ACL_SUCCESS;
}

aclError aclmdlRICaptureEndImpl(aclrtStream stream, aclmdlRI *modelRI)
{
    return ACL_SUCCESS;
}

aclError aclrtSynchronizeStreamImpl(aclrtStream stream)
{
    return ACL_SUCCESS;
}

aclError aclrtGetFunctionAttributeImpl(aclrtFuncHandle funcHandle, aclrtFuncAttribute attr, int64_t *value)
{
    *value = static_cast<int64_t>(aclrtKernelType::ACL_KERNEL_TYPE_VECTOR);
    return ACL_SUCCESS;
}

aclError aclrtStreamAbortImpl(aclrtStream stream)
{
    return ACL_SUCCESS;
}
