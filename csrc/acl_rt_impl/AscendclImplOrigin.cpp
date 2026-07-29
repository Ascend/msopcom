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

#include "AscendclImplOrigin.h"
#include "core/FunctionLoader.h"
#include "core/HijackedLayerManager.h"
#include "utils/InjectLogger.h"
#include "acl_rt_impl/AclRuntimeConfig.h"

#define LOAD_FUNCTION_BODY(soName, funcName, ...) \
    { \
        /* 屏蔽 runtime 接口劫持，防止因劫持接口内调用了 runtime 接口污染记录信息 */ \
        LayerGuard guard(HijackedLayerManager::Instance(), #funcName); \
        FUNC_BODY(soName, funcName, Origin, ACL_ERROR_INTERNAL_ERROR, __VA_ARGS__); \
    }

void AscendclImplOriginCtor()
{
    REGISTER_LIBRARY(AclRuntimeLibName());
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtSetDeviceImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtGetDeviceImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtResetDeviceForceImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtGetPhyDevIdByLogicDevIdImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtGetSocNameImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMallocImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMallocAlign32Impl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtFreeImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMemsetImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMemcpyImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryLoadFromFileImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtCreateBinaryImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryLoadImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryUnLoadImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtDestroyBinaryImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryGetFunctionImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryGetFunctionByEntryImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtRegisterCpuFuncImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsInitImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsInitByUserMemImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsGetMemSizeImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsGetHandleMemSizeImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsAppendImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsAppendPlaceHolderImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsGetPlaceHolderBufferImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsParaUpdateImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsFinalizeImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtLaunchKernelWithConfigImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtLaunchKernelImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtLaunchKernelV2Impl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtGetFunctionAddrImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtSynchronizeStreamWithTimeoutImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtSynchronizeStreamImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtStreamAbortImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMemcpyAsyncImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMallocHostImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtFreeHostImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtCtxGetCurrentDefaultStreamImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclmdlRICaptureBeginImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclmdlRICaptureEndImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclmdlRIDestroyImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclmdlRIExecuteAsyncImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtCmoAsyncImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtLaunchKernelWithHostArgsImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryLoadFromDataImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtGetFunctionAttributeImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtLaunchCallbackImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtProcessReportImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtSubscribeReportImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtGetFunctionNameImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtFunctionGetBinaryImpl);
}

aclError aclrtFunctionGetBinaryImplOrigin(const aclrtFuncHandle funcHandle, aclrtBinHandle *binHandle)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtFunctionGetBinaryImpl, funcHandle, binHandle);
}

aclError aclrtGetFunctionNameImplOrigin(aclrtFuncHandle funcHandle, uint32_t maxLen, char *name)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtGetFunctionNameImpl, funcHandle, maxLen, name);
}

aclError aclrtSetDeviceImplOrigin(int32_t deviceId)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtSetDeviceImpl, deviceId);
}

aclError aclrtGetDeviceImplOrigin(int32_t *deviceId)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtGetDeviceImpl, deviceId);
}

aclError aclrtResetDeviceForceImplOrigin(int32_t deviceId)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtResetDeviceForceImpl, deviceId);
}

aclError aclrtGetPhyDevIdByLogicDevIdImplOrigin(const int32_t logicDevId, int32_t *const phyDevId) {
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtGetPhyDevIdByLogicDevIdImpl, logicDevId, phyDevId);
}

const char *aclrtGetSocNameImplOrigin()
{
    using FuncType = decltype(&aclrtGetSocNameImpl);
    static FuncType func = nullptr;
    if (func == nullptr) {
        func = reinterpret_cast<FuncType>(GET_FUNCTION(AclRuntimeLibName(), "aclrtGetSocNameImpl"));
        if (func == nullptr) {
            return nullptr;
        }
    }
    return func();
}

aclError aclrtMallocImplOrigin(void **devPtr, size_t size, aclrtMemMallocPolicy policy)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtMallocImpl, devPtr, size, policy);
}

aclError aclrtMallocAlign32ImplOrigin(void **devPtr, size_t size, aclrtMemMallocPolicy policy) {
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtMallocAlign32Impl, devPtr, size, policy);
}

aclError aclrtFreeImplOrigin(void *devPtr)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtFreeImpl, devPtr);
}

aclError aclrtMemsetImplOrigin(void *devPtr, size_t maxCount, int32_t value, size_t count)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtMemsetImpl, devPtr, maxCount, value, count);
}

aclError aclrtMemcpyImplOrigin(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtMemcpyImpl, dst, destMax, src, count, kind);
}

aclError aclrtBinaryLoadFromFileImplOrigin(const char* binPath, aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtBinaryLoadFromFileImpl, binPath, options, binHandle);
}

aclrtBinary aclrtCreateBinaryImplOrigin(const void *data, size_t dataLen)
{
    FUNC_BODY(AclRuntimeLibName(), aclrtCreateBinaryImpl, Origin, nullptr, data, dataLen);
}

aclError aclrtBinaryLoadImplOrigin(const aclrtBinary binary, aclrtBinHandle *binHandle)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtBinaryLoadImpl, binary, binHandle);
}

aclError aclrtBinaryUnLoadImplOrigin(aclrtBinHandle binHandle)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtBinaryUnLoadImpl, binHandle);
}

aclError aclrtDestroyBinaryImplOrigin(aclrtBinary binary)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtDestroyBinaryImpl, binary);
}

aclError aclrtBinaryGetFunctionImplOrigin(const aclrtBinHandle binHandle, const char *kernelName, aclrtFuncHandle *funcHandle)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtBinaryGetFunctionImpl, binHandle, kernelName, funcHandle);
}

aclError aclrtBinaryGetFunctionByEntryImplOrigin(aclrtBinHandle binHandle, uint64_t funcEntry, aclrtFuncHandle *funcHandle)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtBinaryGetFunctionByEntryImpl, binHandle, funcEntry, funcHandle);
}

aclError aclrtRegisterCpuFuncImplOrigin(const aclrtBinHandle binHandle, const char *funcName, const char *kernelName, aclrtFuncHandle *funcHandle)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtRegisterCpuFuncImpl, binHandle, funcName, kernelName, funcHandle);
}

aclError aclrtKernelArgsInitImplOrigin(aclrtFuncHandle funcHandle, aclrtArgsHandle *argsHandle)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtKernelArgsInitImpl, funcHandle, argsHandle);
}

aclError aclrtKernelArgsInitByUserMemImplOrigin(aclrtFuncHandle funcHandle, aclrtArgsHandle argsHandle, void *userHostMem, size_t actualArgsSize)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtKernelArgsInitByUserMemImpl, funcHandle, argsHandle, userHostMem, actualArgsSize);
}

aclError aclrtKernelArgsGetMemSizeImplOrigin(aclrtFuncHandle funcHandle, size_t userArgsSize, size_t *actualArgsSize)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtKernelArgsGetMemSizeImpl, funcHandle, userArgsSize, actualArgsSize);
}

aclError aclrtKernelArgsGetHandleMemSizeImplOrigin(aclrtFuncHandle funcHandle, size_t *memSize)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtKernelArgsGetHandleMemSizeImpl, funcHandle, memSize);
}

aclError aclrtKernelArgsAppendImplOrigin(aclrtArgsHandle argsHandle, void *param, size_t paramSize, aclrtParamHandle *paramHandle)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtKernelArgsAppendImpl, argsHandle, param, paramSize, paramHandle);
}

aclError aclrtKernelArgsAppendPlaceHolderImplOrigin(aclrtArgsHandle argsHandle, aclrtParamHandle *paramHandle)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtKernelArgsAppendPlaceHolderImpl, argsHandle, paramHandle);
}

aclError aclrtKernelArgsGetPlaceHolderBufferImplOrigin(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle, size_t dataSize, void **bufferAddr)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtKernelArgsGetPlaceHolderBufferImpl, argsHandle, paramHandle, dataSize, bufferAddr);
}

aclError aclrtKernelArgsParaUpdateImplOrigin(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle, void *param, size_t paramSize)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtKernelArgsParaUpdateImpl, argsHandle, paramHandle, param, paramSize);
}

aclError aclrtKernelArgsFinalizeImplOrigin(aclrtArgsHandle argsHandle)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtKernelArgsFinalizeImpl, argsHandle);
}

aclError aclrtLaunchKernelWithConfigImplOrigin(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream, aclrtLaunchKernelCfg *cfg, aclrtArgsHandle argsHandle, void *reserve)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtLaunchKernelWithConfigImpl, funcHandle, blockDim, stream, cfg, argsHandle, reserve);
}

aclError aclrtLaunchKernelImplOrigin(aclrtFuncHandle funcHandle, uint32_t blockDim, const void *argsData, size_t argsSize, aclrtStream stream)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtLaunchKernelImpl, funcHandle, blockDim, argsData, argsSize, stream);
}

aclError aclrtLaunchKernelV2ImplOrigin(aclrtFuncHandle funcHandle, uint32_t numBlocks, const void *argsData, size_t argsSize, aclrtLaunchKernelCfg *cfg, aclrtStream stream)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtLaunchKernelV2Impl, funcHandle, numBlocks, argsData, argsSize, cfg, stream);
}

aclError aclrtLaunchKernelWithHostArgsImplOrigin(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream,
    aclrtLaunchKernelCfg *cfg, void *hostArgs, size_t argsSize, aclrtPlaceHolderInfo *placeHolderArray, size_t placeHolderNum)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtLaunchKernelWithHostArgsImpl, funcHandle, blockDim, stream, cfg, hostArgs,
                       argsSize, placeHolderArray, placeHolderNum);
}

aclError aclrtBinaryLoadFromDataImplOrigin(const void *data, size_t length, const aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtBinaryLoadFromDataImpl, data, length, options, binHandle);
}

aclError aclmdlRICaptureBeginImplOrigin(aclrtStream stream, aclmdlRICaptureMode mode)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclmdlRICaptureBeginImpl, stream, mode);
}

aclError aclmdlRICaptureEndImplOrigin(aclrtStream stream, aclmdlRI *modelRI)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclmdlRICaptureEndImpl, stream, modelRI);
}

aclError aclmdlRIDestroyImplOrigin(aclmdlRI modelRI)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclmdlRIDestroyImpl, modelRI);
}

aclError aclmdlRIExecuteAsyncImplOrigin(aclmdlRI modelRI, aclrtStream stream)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclmdlRIExecuteAsyncImpl, modelRI, stream);
}

aclError aclrtSynchronizeStreamWithTimeoutImplOrigin(aclrtStream stream, int32_t timeout)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtSynchronizeStreamWithTimeoutImpl, stream, timeout);
}

aclError aclrtStreamAbortImplOrigin(aclrtStream stream)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtStreamAbortImpl, stream);
}

aclError aclrtSynchronizeStreamImplOrigin(aclrtStream stream)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtSynchronizeStreamImpl, stream);
}

aclError aclrtMemcpyAsyncImplOrigin(void *dst, size_t destMax, const void *src, size_t count,
                                    aclrtMemcpyKind kind, aclrtStream stream)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtMemcpyAsyncImpl, dst, destMax, src, count, kind, stream);
}

aclError aclrtMallocHostImplOrigin(void **hostPtr, size_t size)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtMallocHostImpl, hostPtr, size);
}

aclError aclrtFreeHostImplOrigin(void *hostPtr)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtFreeHostImpl, hostPtr);
}

aclError aclrtCtxGetCurrentDefaultStreamImplOrigin(aclrtStream *stream)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtCtxGetCurrentDefaultStreamImpl, stream);
}

aclError aclrtGetFunctionAddrImplOrigin(aclrtFuncHandle funcHandle, void **aicAddr, void **aivAddr)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtGetFunctionAddrImpl, funcHandle, aicAddr, aivAddr);
}

aclError aclrtCmoAsyncImplOrigin(void *src, size_t size, aclrtCmoType cmoType, aclrtStream stream)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtCmoAsyncImpl, src, size, cmoType, stream);
}

aclError aclrtGetFunctionAttributeImplOrigin(aclrtFuncHandle funcHandle, aclrtFuncAttribute attr, int64_t *value)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtGetFunctionAttributeImpl, funcHandle, attr, value);
}

aclError aclrtLaunchCallbackImplOrigin(aclrtCallback fn, void *userData, aclrtCallbackBlockType blockType, aclrtStream stream) {
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtLaunchCallbackImpl, fn, userData, blockType, stream);
}

aclError aclrtProcessReportImplOrigin(int32_t timeout)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtProcessReportImpl, timeout);
}

aclError aclrtSubscribeReportImplOrigin(uint64_t threadId, aclrtStream stream)
{
    LOAD_FUNCTION_BODY(AclRuntimeLibName(), aclrtSubscribeReportImpl, threadId, stream);
}
