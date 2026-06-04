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

// 这个文件将被劫持的接口改名

#ifndef __RUNTIME_ORIGIN_H__
#define __RUNTIME_ORIGIN_H__

#include <string>
#include <iostream>

#include "runtime.h"
#include "core/FunctionLoader.h"

void RuntimeOriginCtor();
RTS_API rtError_t rtFreeOrigin(void *devPtr);
RTS_API rtError_t rtMallocOrigin(void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId);
RTS_API rtError_t rtMemsetOrigin(void *devPtr, uint64_t destMax, uint32_t val, uint64_t cnt);
RTS_API rtError_t rtMemcpyOrigin(void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind);
RTS_API rtError_t rtMemcpyAsyncOrigin(void *dst, uint64_t destMax, const void *src, uint64_t cnt,
    rtMemcpyKind_t kind, rtStream_t stm);
RTS_API rtError_t rtRegisterAllKernelOrigin(const rtDevBinary_t *bin, void **hdl);
RTS_API rtError_t rtDevBinaryRegisterOrigin(const rtDevBinary_t *bin, void **hdl);
RTS_API rtError_t rtDevBinaryUnRegisterOrigin(void *hdl);
RTS_API rtError_t rtFunctionRegisterOrigin(void *binHandle, const void *stubFunc, const char *stubName,
    const void *kernelInfoExt, uint32_t funcMode);
RTS_API rtError_t rtKernelLaunchWithHandleV2Origin(void *hdl, const uint64_t tilingKey, uint32_t blockDim,
    rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo);
RTS_API rtError_t rtAicpuKernelLaunchExWithArgsOrigin(const uint32_t kernelType, const char *const opName,
    const uint32_t blockDim, const rtAicpuArgsEx_t *argsInfo, rtSmDesc_t *const smDesc, const rtStream_t stm,
    const uint32_t flags);
RTS_API rtError_t rtKernelLaunchOrigin(const void *stubFunc, uint32_t blockDim, void *args,
    uint32_t argsSize, rtSmDesc_t *smDesc, rtStream_t stm);
RTS_API rtError_t rtKernelLaunchWithFlagV2Origin(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
    rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags, const rtTaskCfgInfo_t *cfgInfo);
RTS_API rtError_t rtStreamSynchronizeOrigin(rtStream_t stm);
RTS_API rtError_t rtCtxGetCurrentDefaultStreamOrigin(rtStream_t *stm);
RTS_API rtError_t rtKernelGetAddrAndPrefCntOrigin(void *hdl, const uint64_t tilingKey, const void * const stubFunc,
    const uint32_t flag, void **addr, uint32_t *prefetchCnt);
RTS_API rtError_t rtProfSetProSwitchOrigin(void *data, uint32_t len);
RTS_API rtError_t rtGetSocVersionOrigin(char *ver, const uint32_t maxLen);
RTS_API rtError_t rtGetVisibleDeviceIdByLogicDeviceIdOrigin(const int32_t logicDeviceId,
    int32_t *const visibleDeviceId);
RTS_API rtError_t rtStreamCreateOrigin(rtStream_t *stream, int32_t priority);
RTS_API rtError_t rtStreamDestroyOrigin(rtStream_t stream);
RTS_API rtError_t rtGetC2cCtrlAddrOrigin(uint64_t *addr, uint32_t *fftsLen);
RTS_API rtError_t rtMallocHostOrigin(void **hostPtr, uint64_t size, const uint16_t moduleId);
RTS_API rtError_t rtFreeHostOrigin(void *hostPtr);
RTS_API rtError_t rtStreamSynchronizeWithTimeoutOrigin(rtStream_t stream, int32_t timeout);
RTS_API rtError_t rtDeviceStatusQueryOrigin(const uint32_t devId, rtDeviceStatus *deviceStatus);
RTS_API rtError_t rtGetDeviceOrigin(int32_t* devId);
RTS_API rtError_t rtSetDeviceOrigin(int32_t devId);
RTS_API rtError_t rtCallbackLaunchOrigin(rtCallback_t callBackFunc, void *fnData, rtStream_t stm, bool isBlock);
RTS_API rtError_t rtSubscribeReportOrigin(uint64_t threadId, rtStream_t stm);
RTS_API rtError_t rtProcessReportOrigin(int32_t timeout);
RTS_API rtError_t rtUnSubscribeReportOrigin(uint64_t threadId, rtStream_t stm);
RTS_API rtError_t rtModelBindStreamOrigin(rtModel_t mdl, rtStream_t stm, uint32_t flag);
RTS_API rtError_t rtGetL2CacheOffsetOrigin(uint32_t deviceId, uint64_t *offset);
RTS_API rtError_t rtDeviceResetForceOrigin(int32_t devId);
RTS_API rtError_t rtDeviceSetLimitOrigin(int32_t devId, rtLimitType_t type, uint32_t val);
#endif // __RUNTIME_ORIGIN_H__
