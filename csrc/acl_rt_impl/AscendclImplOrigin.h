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
#include <string>

#include "acl.h"
#include "core/FunctionLoader.h"

void AscendclImplOriginCtor();

aclError aclrtSetDeviceImplOrigin(int32_t deviceId);
aclError aclrtGetDeviceImplOrigin(int32_t *deviceId);
aclError aclrtResetDeviceForceImplOrigin(int32_t deviceId);
const char *aclrtGetSocNameImplOrigin();

aclError aclrtMallocImplOrigin(void **devPtr, size_t size, aclrtMemMallocPolicy policy);
aclError aclrtFreeImplOrigin(void *devPtr);
aclError aclrtMemsetImplOrigin(void *devPtr, size_t maxCount, int32_t value, size_t count);
aclError aclrtMemcpyImplOrigin(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind);

aclError aclrtBinaryLoadFromFileImplOrigin(const char* binPath, aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle);

aclrtBinary aclrtCreateBinaryImplOrigin(const void *data, size_t dataLen);
aclError aclrtBinaryLoadImplOrigin(const aclrtBinary binary, aclrtBinHandle *binHandle);
aclError aclrtBinaryLoadFromDataImplOrigin(const void *data, size_t length, const aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle);
aclError aclrtBinaryUnLoadImplOrigin(aclrtBinHandle binHandle);
aclError aclrtDestroyBinaryImplOrigin(aclrtBinary binary);

aclError aclrtBinaryGetFunctionImplOrigin(const aclrtBinHandle binHandle, const char *kernelName, aclrtFuncHandle *funcHandle);
aclError aclrtBinaryGetFunctionByEntryImplOrigin(aclrtBinHandle binHandle, uint64_t funcEntry, aclrtFuncHandle *funcHandle);
aclError aclrtRegisterCpuFuncImplOrigin(const aclrtBinHandle binHandle, const char *funcName, const char *kernelName, aclrtFuncHandle *funcHandle);

aclError aclrtKernelArgsInitImplOrigin(aclrtFuncHandle funcHandle, aclrtArgsHandle *argsHandle);
aclError aclrtKernelArgsInitByUserMemImplOrigin(aclrtFuncHandle funcHandle, aclrtArgsHandle argsHandle, void *userHostMem, size_t actualArgsSize);
aclError aclrtKernelArgsGetMemSizeImplOrigin(aclrtFuncHandle funcHandle, size_t userArgsSize, size_t *actualArgsSize);
aclError aclrtKernelArgsGetHandleMemSizeImplOrigin(aclrtFuncHandle funcHandle, size_t *memSize);
aclError aclrtKernelArgsAppendImplOrigin(aclrtArgsHandle argsHandle, void *param, size_t paramSize, aclrtParamHandle *paramHandle);
aclError aclrtKernelArgsAppendPlaceHolderImplOrigin(aclrtArgsHandle argsHandle, aclrtParamHandle *paramHandle);
aclError aclrtKernelArgsGetPlaceHolderBufferImplOrigin(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle, size_t dataSize, void **bufferAddr);
aclError aclrtKernelArgsParaUpdateImplOrigin(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle, void *param, size_t paramSize);
aclError aclrtKernelArgsFinalizeImplOrigin(aclrtArgsHandle argsHandle);
aclError aclrtLaunchKernelWithConfigImplOrigin(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream, aclrtLaunchKernelCfg *cfg, aclrtArgsHandle argsHandle, void *reserve);
aclError aclrtLaunchKernelImplOrigin(aclrtFuncHandle funcHandle, uint32_t blockDim, const void *argsData, size_t argsSize, aclrtStream stream);
aclError aclrtLaunchKernelV2ImplOrigin(aclrtFuncHandle funcHandle, uint32_t numBlocks, const void *argsData, size_t argsSize, aclrtLaunchKernelCfg *cfg, aclrtStream stream);
aclError aclrtLaunchKernelWithHostArgsImplOrigin(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream, aclrtLaunchKernelCfg *cfg, void *hostArgs,
    size_t argsSize, aclrtPlaceHolderInfo *placeHolderArray, size_t placeHolderNum);
aclError aclmdlRICaptureBeginImplOrigin(aclrtStream stream, aclmdlRICaptureMode mode);
aclError aclmdlRICaptureEndImplOrigin(aclrtStream stream, aclmdlRI *modelRI);
aclError aclmdlRIDestroyImplOrigin(aclmdlRI modelRI);
aclError aclmdlRIExecuteAsyncImplOrigin(aclmdlRI modelRI, aclrtStream stream);
aclError aclrtGetFunctionAddrImplOrigin(aclrtFuncHandle funcHandle, void **aicAddr, void **aivAddr);
aclError aclrtSynchronizeStreamWithTimeoutImplOrigin(aclrtStream stream, int32_t timeout);
aclError aclrtStreamAbortImplOrigin(aclrtStream stream);
aclError aclrtSynchronizeStreamImplOrigin(aclrtStream stream);
aclError aclrtMemcpyAsyncImplOrigin(void *dst, size_t destMax, const void *src, size_t count,
                                    aclrtMemcpyKind kind, aclrtStream stream);
aclError aclrtMallocHostImplOrigin(void **hostPtr, size_t size);
aclError aclrtFreeHostImplOrigin(void *hostPtr);
aclError aclrtCtxGetCurrentDefaultStreamImplOrigin(aclrtStream *stream);
aclError aclrtCmoAsyncImplOrigin(void *src, size_t size, aclrtCmoType cmoType, aclrtStream stream);
aclError aclrtGetFunctionAttributeImplOrigin(aclrtFuncHandle funcHandle, aclrtFuncAttribute attr, int64_t *value);
