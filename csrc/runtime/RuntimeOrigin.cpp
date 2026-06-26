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


#include "RuntimeOrigin.h"
#include "core/FunctionLoader.h"
#include "RuntimeConfig.h"
#define LOAD_FUNCTION_BODY(soName, funcName, ...)                           \
    FUNC_BODY(soName, funcName, Origin, RT_ERROR_RESERVED, __VA_ARGS__)     \
// 定义下面的函数本身的原始动态库名
void RuntimeOriginCtor()
{
    std::string soName = RuntimeLibName();
    REGISTER_LIBRARY(soName);
    REGISTER_FUNCTION(soName, rtFree);
    REGISTER_FUNCTION(soName, rtMalloc);
    REGISTER_FUNCTION(soName, rtMemset);
    REGISTER_FUNCTION(soName, rtMemcpy);
    REGISTER_FUNCTION(soName, rtMemcpyAsync);
    REGISTER_FUNCTION(soName, rtGetVisibleDeviceIdByLogicDeviceId);
    REGISTER_FUNCTION(soName, rtRegisterAllKernel);
    REGISTER_FUNCTION(soName, rtDevBinaryRegister);
    REGISTER_FUNCTION(soName, rtDevBinaryUnRegister);
    REGISTER_FUNCTION(soName, rtFunctionRegister);
    REGISTER_FUNCTION(soName, rtKernelLaunch);
    REGISTER_FUNCTION(soName, rtKernelLaunchWithHandleV2);
    REGISTER_FUNCTION(soName, rtKernelLaunchWithFlagV2);
    REGISTER_FUNCTION(soName, rtProfSetProSwitch);
    REGISTER_FUNCTION(soName, rtStreamSynchronize);
    REGISTER_FUNCTION(soName, rtCtxGetCurrentDefaultStream);
    REGISTER_FUNCTION(soName, rtKernelGetAddrAndPrefCnt);
    REGISTER_FUNCTION(soName, rtGetSocVersion);
    REGISTER_FUNCTION(soName, rtStreamCreate);
    REGISTER_FUNCTION(soName, rtStreamDestroy);
    REGISTER_FUNCTION(soName, rtGetC2cCtrlAddr);
    REGISTER_FUNCTION(soName, rtFreeHost);
    REGISTER_FUNCTION(soName, rtMallocHost);
    REGISTER_FUNCTION(soName, rtStreamSynchronizeWithTimeout);
    REGISTER_FUNCTION(soName, rtDeviceStatusQuery);
    REGISTER_FUNCTION(soName, rtGetDevice);
    REGISTER_FUNCTION(soName, rtSetDevice);
    REGISTER_FUNCTION(soName, rtCallbackLaunch);
    REGISTER_FUNCTION(soName, rtProcessReport);
    REGISTER_FUNCTION(soName, rtSubscribeReport);
    REGISTER_FUNCTION(soName, rtUnSubscribeReport);
    REGISTER_FUNCTION(soName, rtModelBindStream);
    REGISTER_FUNCTION(soName, rtGetL2CacheOffset);
    REGISTER_FUNCTION(soName, rtDeviceResetForce);
}

rtError_t rtFreeOrigin(void *devPtr)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtFree, devPtr);
}

rtError_t rtMallocOrigin(void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtMalloc, devPtr, size, type, moduleId);
}

rtError_t rtMemsetOrigin(void *devPtr, uint64_t destMax, uint32_t val, uint64_t cnt)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtMemset, devPtr, destMax, val, cnt);
}

rtError_t rtMemcpyOrigin(void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtMemcpy, dst, destMax, src, cnt, kind);
}

rtError_t rtMemcpyAsyncOrigin(void *dst, uint64_t destMax, const void *src, uint64_t cnt,
    rtMemcpyKind_t kind, rtStream_t stm)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtMemcpyAsync, dst, destMax, src, cnt, kind, stm);
}

rtError_t rtRegisterAllKernelOrigin(const rtDevBinary_t *bin, void **hdl)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtRegisterAllKernel, bin, hdl);
}

rtError_t rtDevBinaryRegisterOrigin(const rtDevBinary_t *bin, void **hdl)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtDevBinaryRegister, bin, hdl);
}

rtError_t rtDevBinaryUnRegisterOrigin(void *hdl)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtDevBinaryUnRegister, hdl);
}

rtError_t rtFunctionRegisterOrigin(void *binHandle, const void *stubFunc, const char *stubName,
    const void *kernelInfoExt, uint32_t funcMode)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtFunctionRegister, binHandle, stubFunc, stubName, kernelInfoExt, funcMode);
}

rtError_t rtKernelLaunchOrigin(const void *stubFunc, uint32_t blockDim,
    void *args, uint32_t argsSize, rtSmDesc_t *smDesc, rtStream_t stm)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtKernelLaunch, stubFunc, blockDim, args, argsSize, smDesc, stm);
}

rtError_t rtKernelLaunchWithHandleV2Origin(void *hdl, const uint64_t tilingKey, uint32_t blockDim,
    rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtKernelLaunchWithHandleV2,
                       hdl, tilingKey, blockDim, argsInfo, smDesc, stm, cfgInfo);
}

rtError_t rtAicpuKernelLaunchExWithArgsOrigin(const uint32_t kernelType, const char *const opName,
    const uint32_t blockDim, const rtAicpuArgsEx_t *argsInfo, rtSmDesc_t *const smDesc, const rtStream_t stm,
    const uint32_t flags)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtAicpuKernelLaunchExWithArgs,
                       kernelType, opName, blockDim, argsInfo, smDesc, stm, flags);
}

rtError_t rtKernelLaunchWithFlagV2Origin(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
    rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags, const rtTaskCfgInfo_t *cfgInfo)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtKernelLaunchWithFlagV2,
                       stubFunc, blockDim, argsInfo, smDesc, stm, flags, cfgInfo);
}

rtError_t rtProfSetProSwitchOrigin(void *data, uint32_t len)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtProfSetProSwitch, data, len);
}

rtError_t rtStreamSynchronizeOrigin(rtStream_t stm)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtStreamSynchronize, stm);
}

rtError_t rtCtxGetCurrentDefaultStreamOrigin(rtStream_t *stm)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtCtxGetCurrentDefaultStream, stm);
}

rtError_t rtKernelGetAddrAndPrefCntOrigin(void *hdl, const uint64_t tilingKey, const void * const stubFunc,
    const uint32_t flag, void **addr, uint32_t *prefetchCnt)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtKernelGetAddrAndPrefCnt, hdl, tilingKey, stubFunc, flag, addr, prefetchCnt);
}

RTS_API rtError_t rtGetSocVersionOrigin(char *ver, const uint32_t maxLen)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtGetSocVersion, ver, maxLen);
}

RTS_API rtError_t rtGetVisibleDeviceIdByLogicDeviceIdOrigin(const int32_t logicDeviceId,
    int32_t *const visibleDeviceId)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtGetVisibleDeviceIdByLogicDeviceId, logicDeviceId,
        visibleDeviceId);
}

RTS_API rtError_t rtStreamCreateOrigin(rtStream_t *stream, int32_t priority)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtStreamCreate, stream, priority);
}

RTS_API rtError_t rtStreamDestroyOrigin(rtStream_t stream)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtStreamDestroy, stream);
}

RTS_API rtError_t rtGetC2cCtrlAddrOrigin(uint64_t *addr, uint32_t *fftsLen)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtGetC2cCtrlAddr, addr, fftsLen);
}

RTS_API rtError_t rtMallocHostOrigin(void **hostPtr, uint64_t size, const uint16_t moduleId)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtMallocHost, hostPtr, size, moduleId);
}

RTS_API rtError_t rtFreeHostOrigin(void *hostPtr)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtFreeHost, hostPtr);
}

RTS_API rtError_t rtStreamSynchronizeWithTimeoutOrigin(rtStream_t stream, int32_t timeout)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtStreamSynchronizeWithTimeout, stream, timeout);
}

RTS_API rtError_t rtDeviceStatusQueryOrigin(const uint32_t devId, rtDeviceStatus *deviceStatus)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtDeviceStatusQuery, devId, deviceStatus);
}

RTS_API rtError_t rtGetDeviceOrigin(int32_t* devId)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtGetDevice, devId);
}

RTS_API rtError_t rtSetDeviceOrigin(int32_t devId)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtSetDevice, devId);
}

RTS_API rtError_t rtCallbackLaunchOrigin(rtCallback_t callBackFunc, void *fnData, rtStream_t stm, bool isBlock)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtCallbackLaunch, callBackFunc, fnData, stm, isBlock);
}

RTS_API rtError_t rtSubscribeReportOrigin(uint64_t threadId, rtStream_t stm)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtSubscribeReport, threadId, stm);
}

RTS_API rtError_t rtProcessReportOrigin(int32_t timeout)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtProcessReport, timeout);
}

RTS_API rtError_t rtUnSubscribeReportOrigin(uint64_t threadId, rtStream_t stm)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtUnSubscribeReport, threadId, stm);
}

RTS_API rtError_t rtModelBindStreamOrigin(rtModel_t mdl, rtStream_t stm, uint32_t flag)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtModelBindStream, mdl, stm, flag);
}

RTS_API rtError_t rtGetL2CacheOffsetOrigin(uint32_t deviceId, uint64_t *offset)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtGetL2CacheOffset, deviceId, offset);
}

RTS_API rtError_t rtDeviceResetForceOrigin(int32_t devId)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtDeviceResetForce, devId);
}

RTS_API rtError_t rtDeviceSetLimitOrigin(int32_t devId, rtLimitType_t type, uint32_t val)
{
    LOAD_FUNCTION_BODY(RuntimeLibName(), rtDeviceSetLimit, devId, type, val);
}
