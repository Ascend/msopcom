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


// 该文件中本身用于关联工具和劫持接口
#include <dlfcn.h>
#include "runtime/HijackedFunc.h"
#include "runtime/inject_helpers/MsTx.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/RuntimeOrigin.h"
#include "runtime/RuntimeConfig.h"
#include "runtime/inject_helpers/BBCountDumper.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "ascend_hal/HijackedFunc.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "runtime.h"
#include "utils/Ustring.h"
#include "utils/FileSystem.h"
#include "ascend_dump/HijackedFunc.h"
#include "hccl/HijackedFunc.h"
#include "hccl/HcclOrigin.h"
#include "ascendcl/AscendclOrigin.h"
#include "profapi/HijackedFunc.h"
#include "camodel/Camodel.h"
#include "core/HijackedLayerManager.h"
#include "acl_rt_impl/HijackedFunc.h"
#include "profapi/ProfOriginal.h"
#include "ascend_hal/AscendHalOrigin.h"
#include "profapi/RegisterMsopprofProfileCallback.h"
static bool g_isCtorDone = false;

#define PRINT_ENTER_INSTRUMENTOR DEBUG_LOG("enter %s", __FUNCTION__)

namespace {
void RegisterHccl()
{
    std::string soName = "hccl";
    REGISTER_LIBRARY(soName);
    REGISTER_FUNCTION(soName, HcclCommInitRootInfo);
    REGISTER_FUNCTION(soName, HcclCommInitRootInfoConfig);
    REGISTER_FUNCTION(soName, HcclCommInitAll);
    REGISTER_FUNCTION(soName, HcclGetRootInfo);
    HcclOriginCtor();
}

void RegisterProfApi()
{
    std::string soName = "profapi";
    REGISTER_LIBRARY(soName);
    REGISTER_FUNCTION(soName, MsprofRegisterCallback);
    REGISTER_FUNCTION(soName, MsprofNotifySetDevice);
    REGISTER_FUNCTION(soName, profSetProfCommand);
    REGISTER_FUNCTION(soName, MsprofRegisterProfileCallback);
}

void RegisterAscendDump()
{
    std::string soName = "ascend_dump";
    REGISTER_LIBRARY(soName);
    REGISTER_FUNCTION(soName, AdumpGetDFXInfoAddrForDynamic);
}

void RegisterAscendCl()
{
    REGISTER_LIBRARY(AclRuntimeLibName());
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtSetDeviceImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtResetDeviceImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMallocImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtFreeImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMapMemImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtUnmapMemImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtGetDeviceImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtCreateContextImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtQueryDeviceStatusImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtGetDeviceCountImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtGetDeviceInfoImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtGetSocNameImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryLoadFromFileImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryGetFunctionImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtLaunchKernelWithConfigImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtLaunchKernelImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtLaunchKernelV2Impl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsAppendImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsAppendPlaceHolderImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsGetPlaceHolderBufferImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsFinalizeImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsInitImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsInitByUserMemImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMallocWithCfgImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsParaUpdateImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryGetFunctionByEntryImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtLaunchKernelWithHostArgsImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryLoadFromDataImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtCreateBinaryImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryLoadImpl);
}

// 上板注册libruntime.so，仿真注册libruntime_camodel.so
void RegisterRuntime()
{
    std::string soName;
    bool isSimulator = (GetEnv(IS_SIMULATOR_ENV) == "true");
    if (isSimulator) {
        soName = "runtime_camodel";
    } else {
        soName = "runtime";
        KernelMatcher::Config config;
        BBCountDumper::Instance().Init(GetEnv(DEVICE_PROF_DUMP_PATH_ENV), config);
    }
    RuntimeConfig::Instance().soName_ = soName;
    REGISTER_LIBRARY(soName);
    REGISTER_FUNCTION(soName, rtKernelLaunch);
    REGISTER_FUNCTION(soName, rtMalloc);
    REGISTER_FUNCTION(soName, rtFree);
    REGISTER_FUNCTION(soName, rtMapMem);
    REGISTER_FUNCTION(soName, rtUnmapMem);
    REGISTER_FUNCTION(soName, rtKernelLaunchWithHandleV2);
    REGISTER_FUNCTION(soName, rtKernelLaunchWithFlagV2);
    REGISTER_FUNCTION(soName, rtFunctionRegister);
    REGISTER_FUNCTION(soName, rtDevBinaryRegister);
    REGISTER_FUNCTION(soName, rtRegisterAllKernel);
    REGISTER_FUNCTION(soName, rtSetDevice);
    REGISTER_FUNCTION(soName, rtDeviceReset);
    REGISTER_FUNCTION(soName, rtSetDeviceEx);
    REGISTER_FUNCTION(soName, rtSetDeviceV2);
    REGISTER_FUNCTION(soName, rtCtxCreate);
    REGISTER_FUNCTION(soName, rtCtxCreateV2);
    REGISTER_FUNCTION(soName, rtCtxCreateEx);
    REGISTER_FUNCTION(soName, rtGetDevice);
    REGISTER_FUNCTION(soName, rtGetSocVersion);
    REGISTER_FUNCTION(soName, rtGetAiCoreCount);
    REGISTER_FUNCTION(soName, rtGetDeviceCount);
    REGISTER_FUNCTION(soName, rtSetExceptionExtInfo);
    REGISTER_FUNCTION(soName, rtAicpuKernelLaunchExWithArgs);
    REGISTER_FUNCTION(soName, rtGetDeviceInfo);
    REGISTER_FUNCTION(soName, rtKernelLaunchWithHandle);
    REGISTER_FUNCTION(soName, rtDeviceResetEx);
    REGISTER_FUNCTION(soName, rtDeviceStatusQuery);
    if (isSimulator) {
        GET_FUNCTION(soName, "rtSetDevice");
    }
}

void RegisterAscendHal()
{
    std::string soName = "ascend_hal";
    REGISTER_LIBRARY(soName);
    REGISTER_FUNCTION(soName, halGetSocVersion);
}

void __attribute__ ((constructor)) HijackedCtor()
{
    FuncSelector::Instance().Set(ToolType::PROF);

    RegisterRuntime();
    RuntimeOriginCtor();
    AscendclImplOriginCtor();
    AscendclOriginCtor();
    RegisterAscendCl();
    RegisterHccl();
    RegisterProfApi();
    RegisterAscendDump();
    RegisterAscendHal();
    bool isSimulator = (GetEnv(IS_SIMULATOR_ENV) == "true");
    if (isSimulator) {
        CamodelCtor();
    }
    g_isCtorDone = true;
    RegisterMsopprofProfileCallback::Instance()->RegisterFuncMsprof();
}

using namespace MstxAPI;
namespace {
void MstxGetToolId(uint64_t *id)
{
    if (id == nullptr) {
        return;
    }
    *id = MSTX_TOOL_MSPROFOP_ID;
}
}
}

void *AdumpGetDFXInfoAddrForDynamic(uint32_t space, uint64_t &atomicIndex)
{
    PRINT_ENTER_INSTRUMENTOR;
    if (!g_isCtorDone) {
        // In "ACL_RT_KERNEL_LAUNCH" scenario rtDevBinaryRegister() is invoked before main() and HijackedCtor().
        // HijackedCtor() needs to be invoked manually before rtDevBinaryRegister()
        HijackedCtor();
    }
    HijackedFuncOfAdumpGetDFXInfoAddrForDynamic instance;
    return instance.Call(space, atomicIndex);
}
rtError_t rtMapMem(void *devPtr, size_t size, size_t offset, rtDrvMemHandle_t *handle, uint64_t flags)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfMapMem instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(devPtr, size, offset, handle, flags);
    }
    return instance.Call(devPtr, size, offset, handle, flags);
}

rtError_t rtUnmapMem(void *devPtr)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfUnmapMem instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(devPtr);
    }
    return instance.Call(devPtr);
}

rtError_t rtDevBinaryRegister(const rtDevBinary_t *bin, void **hdl)
{
    PRINT_ENTER_INSTRUMENTOR;
    if (!g_isCtorDone) {
        // In "ACL_RT_KERNEL_LAUNCH" scenario rtDevBinaryRegister() is invoked before main() and HijackedCtor().
        // HijackedCtor() needs to be invoked manually before rtDevBinaryRegister()
        DEBUG_LOG("HijackedCtor invoked in rtDevBinaryRegister");
        HijackedCtor();
    }
    HijackedFuncOfDevBinaryRegister instance;
    return instance.Call(bin, hdl);
}

rtError_t rtRegisterAllKernel(const rtDevBinary_t *bin, void **hdl)
{
    PRINT_ENTER_INSTRUMENTOR;
    if (!g_isCtorDone) {
        // In "ACL_RT_KERNEL_LAUNCH" scenario rtRegisterAllKernel() is invoked before main() and HijackedCtor().
        // HijackedCtor() needs to be invoked manually before rtRegisterAllKernel()
        DEBUG_LOG("HijackedCtor invoked in rtRegisterAllKernel");
        HijackedCtor();
    }
    HijackedFuncOfRegisterAllKernel instance;
    return instance.Call(bin, hdl);
}

rtError_t rtFunctionRegister(void *binHandle, const void *stubFunc, const char *stubName,
                             const void *kernelInfoExt, uint32_t funcMode)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfFunctionRegister instance;
    return instance.Call(binHandle, stubFunc, stubName, kernelInfoExt, funcMode);
}

rtError_t rtKernelLaunch(const void *stubFunc, uint32_t blockDim, void *args,
                         uint32_t argsSize, rtSmDesc_t *smDesc, rtStream_t stm)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfKernelLaunch instance;
    return instance.Call(stubFunc, blockDim, args, argsSize, smDesc, stm);
}

rtError_t rtKernelLaunchWithHandle(void *hdl, const uint64_t tilingKey, uint32_t blockDim, rtArgsEx_t *argsInfo,
                                   rtSmDesc_t *smDesc, rtStream_t stm, const void* kernelInfo)
{
    PRINT_ENTER_INSTRUMENTOR;
    (void)kernelInfo;
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    return instance.Call(hdl, tilingKey, blockDim, argsInfo, smDesc, stm, nullptr);
}

rtError_t rtKernelLaunchWithHandleV2(void *hdl, const uint64_t tilingKey, uint32_t blockDim, rtArgsEx_t *argsInfo,
                                     rtSmDesc_t *smDesc, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    return instance.Call(hdl, tilingKey, blockDim, argsInfo, smDesc, stm, cfgInfo);
}

rtError_t rtAicpuKernelLaunchExWithArgs(const uint32_t kernelType, const char *const opName,
    const uint32_t blockDim, const rtAicpuArgsEx_t *argsInfo, rtSmDesc_t *const smDesc, const rtStream_t stm,
    const uint32_t flags)
{
    HijackedFuncOfAiCpuKernelLaunchExWithArgs instance;
    return instance.Call(kernelType, opName, blockDim, argsInfo, smDesc, stm, flags);
}

rtError_t rtKernelLaunchWithFlagV2(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                                   rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags, const rtTaskCfgInfo_t *cfgInfo)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    return instance.Call(stubFunc, blockDim, argsInfo, smDesc, stm, flags, cfgInfo);
}

rtError_t rtSetDevice(int32_t devId)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfSetDevice instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(devId);
    }
    return instance.Call(devId);
}

rtError_t rtDeviceReset(int32_t devId)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfDeviceReset instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(devId);
    }
    return instance.Call(devId);
}

rtError_t rtDeviceResetEx(int32_t devId)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfDeviceResetEx instance;
    return instance.Call(devId);
}

rtError_t rtSetDeviceEx(int32_t devId)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfSetDeviceEx instance;
    return instance.Call(devId);
}

rtError_t rtCtxCreateEx(void **ctx, uint32_t flags, int32_t devId)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfCtxCreateEx instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(ctx, flags, devId);
    }
    return instance.Call(ctx, flags, devId);
}

rtError_t rtMalloc(void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId)
{
    HijackedFuncOfMalloc instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(devPtr, size, type, moduleId);
    }
    return instance.Call(devPtr, size, type, moduleId);
}

rtError_t rtFree(void *devPtr)
{
    HijackedFuncOfFree instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(devPtr);
    }
    return instance.Call(devPtr);
}

rtError_t rtSetDeviceV2(int32_t devId, rtDeviceMode deviceMode)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfSetDeviceV2 instance;
    return instance.Call(devId, deviceMode);
}

rtError_t rtCtxCreate(void ** createCtx, uint32_t flags, int32_t devId)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfCtxCreate instance;
    return instance.Call(createCtx, flags, devId);
}

rtError_t rtDeviceStatusQuery(const uint32_t devId, rtDeviceStatus *deviceStatus)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfDeviceStatusQuery instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(devId, deviceStatus);
    }
    return instance.Call(devId, deviceStatus);
}

rtError_t rtCtxCreateV2(void **createCtx, uint32_t flags, int32_t devId, rtDeviceMode deviceMode)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfCtxCreateV2 instance;
    return instance.Call(createCtx, flags, devId, deviceMode);
}

rtError_t rtGetDevice(int32_t *devId)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfGetDevice instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(devId);
    }
    return instance.Call(devId);
}

rtError_t rtGetDeviceCount(int32_t *cnt)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfGetDeviceCount instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(cnt);
    }
    return instance.Call(cnt);
}

rtError_t rtGetDeviceInfo(uint32_t deviceId, int32_t moduleType, int32_t infoType, int64_t *val)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfGetDeviceInfo instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(deviceId, moduleType, infoType, val);
    }
    return instance.Call(deviceId, moduleType, infoType, val);
}

rtError_t rtGetSocVersion(char *ver, const uint32_t maxLen)
{
    PRINT_ENTER_INSTRUMENTOR;
    if (!g_isCtorDone) {
        // In "ACL_RT_KERNEL_LAUNCH" scenario rtGetSocVersion() is invoked before main() and HijackedCtor().
        // HijackedCtor() needs to be invoked manually before rtGetSocVersion()
        DEBUG_LOG("HijackedCtor invoked in rtGetSocVersion");
        HijackedCtor();
    }
    HijackedFuncOfGetSocVersion instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(ver, maxLen);
    }
    return instance.Call(ver, maxLen);
}

rtError_t rtGetAiCoreCount(uint32_t *aiCoreCnt)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfGetAiCoreCount instance;
    return instance.Call(aiCoreCnt);
}

rtError_t rtSetExceptionExtInfo(const rtArgsSizeInfo_t *const sizeInfo)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfSetExceptionExtInfo instance;
    return instance.Call(sizeInfo);
}

aclError aclrtSetDeviceImpl(int32_t deviceId)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtSetDeviceImpl instance;
    return instance.Call(deviceId);
}

aclError aclrtResetDeviceImpl(int32_t deviceId)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtResetDeviceImpl instance;
    return instance.Call(deviceId);
}

aclError aclrtMallocImpl(void **devPtr, size_t size, aclrtMemMallocPolicy policy)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtMallocImpl instance;
    return instance.Call(devPtr, size, policy);
}

aclError aclrtFreeImpl(void *devPtr)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtFreeImpl instance;
    return instance.Call(devPtr);
}

aclError aclrtMapMemImpl(void *virPtr, size_t size, size_t offset,
                         aclrtDrvMemHandle handle, uint64_t flags)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtMapMemImpl instance;
    return instance.Call(virPtr, size, offset, handle, flags);
}

aclError aclrtUnmapMemImpl(void *virPtr)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtUnmapMemImpl instance;
    return instance.Call(virPtr);
}

aclError aclrtGetDeviceImpl(int32_t *deviceId)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtGetDeviceImpl instance;
    return instance.Call(deviceId);
}

aclError aclrtCreateContextImpl(aclrtContext *context, int32_t deviceId)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtCreateContextImpl instance;
    return instance.Call(context, deviceId);
}

aclError aclrtQueryDeviceStatusImpl(int32_t deviceId, aclrtDeviceStatus *deviceStatus)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtQueryDeviceStatusImpl instance;
    return instance.Call(deviceId, deviceStatus);
}

aclError aclrtGetDeviceCountImpl(uint32_t *count)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtGetDeviceCountImpl instance;
    return instance.Call(count);
}
aclError aclrtGetDeviceInfoImpl(uint32_t deviceId, aclrtDevAttr attr, int64_t *value)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtGetDeviceInfoImpl instance;
    return instance.Call(deviceId, attr, value);
}
const char *aclrtGetSocNameImpl()
{
    PRINT_ENTER_INSTRUMENTOR;
    if (!g_isCtorDone) {
        // In "ACL_RT_KERNEL_LAUNCH" scenario aclrtGetSocNameImpl() is invoked before main() and HijackedCtor().
        // HijackedCtor() needs to be invoked manually before aclrtGetSocNameImpl()
        DEBUG_LOG("HijackedCtor invoked in aclrtGetSocNameImpl");
        HijackedCtor();
    }
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtGetSocNameImpl instance;
    return instance.Call();
}

aclError aclrtBinaryLoadFromFileImpl(const char *binPath, aclrtBinaryLoadOptions *options,
                                     aclrtBinHandle *binHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtBinaryLoadFromFileImpl instance;
    return instance.Call(binPath, options, binHandle);
}

aclError aclrtBinaryGetFunctionImpl(const aclrtBinHandle binHandle, const char *kernelName,
                                    aclrtFuncHandle *funcHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtBinaryGetFunctionImpl instance;
    return instance.Call(binHandle, kernelName, funcHandle);
}

aclError aclrtBinaryGetFunctionByEntryImpl(aclrtBinHandle binHandle, uint64_t funcEntry, aclrtFuncHandle *funcHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtBinaryGetFunctionByEntryImpl instance;
    return instance.Call(binHandle, funcEntry, funcHandle);
}

aclError aclrtRegisterCpuFuncImpl(const aclrtBinHandle binHandle, const char *funcName, const char *kernelName, aclrtFuncHandle *funcHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtRegisterCpuFuncImpl instance;
    return instance.Call(binHandle, funcName, kernelName, funcHandle);
}

aclError aclrtKernelArgsAppendImpl(aclrtArgsHandle argsHandle, void *param, size_t paramSize,
                                   aclrtParamHandle *paramHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtKernelArgsAppendImpl instance;
    return instance.Call(argsHandle, param, paramSize, paramHandle);
}

aclError aclrtKernelArgsParaUpdateImpl(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle, void *param, size_t paramSize)
{
    HijackedFuncOfAclrtKernelArgsParaUpdateImpl instance;
    return instance.Call(argsHandle, paramHandle, param, paramSize);
}

aclError aclrtKernelArgsFinalizeImpl(aclrtArgsHandle argsHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtKernelArgsFinalizeImpl instance;
    return instance.Call(argsHandle);
}

aclError aclrtKernelArgsAppendPlaceHolderImpl(aclrtArgsHandle argsHandle, aclrtParamHandle *paramHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtKernelArgsAppendPlaceHolderImpl instance;
    return instance.Call(argsHandle, paramHandle);
}

aclError aclrtKernelArgsGetPlaceHolderBufferImpl(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle,
                                                 size_t dataSize, void **bufferAddr)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtKernelArgsGetPlaceHolderBufferImpl instance;
    return instance.Call(argsHandle, paramHandle, dataSize, bufferAddr);
}

aclError aclrtKernelArgsInitImpl(aclrtFuncHandle funcHandle, aclrtArgsHandle *argsHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtKernelArgsInitImpl instance;
    return instance.Call(funcHandle, argsHandle);
}

aclError aclrtKernelArgsInitByUserMemImpl(aclrtFuncHandle funcHandle, aclrtArgsHandle argsHandle,
                                          void *userHostMem, size_t actualArgsSize)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtKernelArgsInitByUserMemImpl instance;
    return instance.Call(funcHandle, argsHandle, userHostMem, actualArgsSize);
}

aclError aclrtMallocWithCfgImpl(void **devPtr, size_t size, aclrtMemMallocPolicy policy, aclrtMallocConfig *cfg)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtMallocWithCfgImpl instance;
    return instance.Call(devPtr, size, policy, cfg);
}

aclError aclrtLaunchKernelWithConfigImpl(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream,
                                         aclrtLaunchKernelCfg *cfg, aclrtArgsHandle argsHandle, void *reserve)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtLaunchKernelWithConfigImpl instance;
    return instance.Call(funcHandle, blockDim, stream, cfg, argsHandle, reserve);
}

aclError aclrtLaunchKernelImpl(aclrtFuncHandle funcHandle, uint32_t blockDim, const void *argsData,
                               size_t argsSize, aclrtStream stream)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtLaunchKernelImpl instance;
    return instance.Call(funcHandle, blockDim, argsData, argsSize, stream);
}

aclError aclrtLaunchKernelV2Impl(aclrtFuncHandle funcHandle, uint32_t numBlocks, const void *argsData,
                                  size_t argsSize, aclrtLaunchKernelCfg *cfg, aclrtStream stream)
{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtLaunchKernelV2Impl instance;
    return instance.Call(funcHandle, numBlocks, argsData, argsSize, cfg, stream);
}

aclError aclrtLaunchKernelWithHostArgsImpl(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream,
    aclrtLaunchKernelCfg *cfg, void *hostArgs, size_t argsSize,
    aclrtPlaceHolderInfo *placeHolderArray, size_t placeHolderNum)

{
    PRINT_ENTER_INSTRUMENTOR;
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl instance;
    return instance.Call(funcHandle, blockDim, stream, cfg, hostArgs, argsSize, placeHolderArray, placeHolderNum);
}

aclError aclrtBinaryLoadFromDataImpl(const void *data, size_t length, const aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    if (!g_isCtorDone) {
        // aclrtBinaryLoadFromDataImpl may be called before HijackedCtor initialized
        DEBUG_LOG("HijackedCtor invoked in aclrtBinaryLoadFromDataImpl");
        HijackedCtor();
    }
    HijackedFuncOfAclrtBinaryLoadFromDataImpl instance;
    return instance.Call(data, length, options, binHandle);
}

aclrtBinary aclrtCreateBinaryImpl(const void *data, size_t dataLen)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtCreateBinaryImpl instance;
    return instance.Call(data, dataLen);
}

aclError aclrtBinaryLoadImpl(const aclrtBinary binary, aclrtBinHandle *binHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtBinaryLoadImpl instance;
    return instance.Call(binary, binHandle);
}


HcclResult HcclCommInitClusterInfo(const char *clusterInfo, uint32_t rank, HcclComm *comm)
{
    PRINT_ENTER_INSTRUMENTOR;
    if (!g_isCtorDone) {
        HijackedCtor();
    }
    HijackedFuncOfHcclCommInitClusterInfo instance;
    return instance.Call(clusterInfo, rank, comm);
}

HcclResult HcclCommInitClusterInfoConfig(const char *clusterInfo, uint32_t rank, HcclCommConfig *config,
                                         HcclComm *comm)
{
    PRINT_ENTER_INSTRUMENTOR;
    if (!g_isCtorDone) {
        HijackedCtor();
    }
    HijackedFuncOfHcclCommInitClusterInfoConfig instance;
    return instance.Call(clusterInfo, rank, config, comm);
}

HcclResult HcclCommInitRootInfo(uint32_t nRanks, const HcclRootInfo *rootInfo, uint32_t rank, HcclComm *comm)
{
    PRINT_ENTER_INSTRUMENTOR;
    if (!g_isCtorDone) {
        HijackedCtor();
    }
    HijackedFuncOfHcclCommInitRootInfo instance;
    return instance.Call(nRanks, rootInfo, rank, comm);
}

HcclResult HcclCommInitRootInfoConfig(uint32_t nRanks, const HcclRootInfo *rootInfo, uint32_t rank,
                                      const HcclCommConfig *config, HcclComm *comm)
{
    PRINT_ENTER_INSTRUMENTOR;
    if (!g_isCtorDone) {
        HijackedCtor();
    }
    HijackedFuncOfHcclCommInitRootInfoConfig instance;
    return instance.Call(nRanks, rootInfo, rank, config, comm);
}

int32_t MsprofRegisterCallback(uint32_t moduleId, ProfCommandHandle handle)
{
    PRINT_ENTER_INSTRUMENTOR;
    if (!g_isCtorDone) {
        HijackedCtor();
    }
    HijackedFuncOfMsprofRegisterCallback instance;
    return instance.Call(moduleId, handle);
}

int32_t MsprofNotifySetDevice(uint32_t chipId, uint32_t deviceId, bool isOpen)
{
    PRINT_ENTER_INSTRUMENTOR;
    if (!g_isCtorDone) {
        HijackedCtor();
    }
    HijackedFuncOfMsprofNotifySetDevice instance;
    return instance.Call(chipId, deviceId, isOpen);
}

/*************** implementation of mstx api by msprof ***************/

using namespace MstxAPI;

static MstxRangeId MstxRangeStartA(const char* message, AclrtStream stream)
{
    (void)stream;
    if (message == nullptr || std::string(message).empty() ||
        !CheckInputStringValid(std::string(message), MAX_MESSAGE_LENGTH)) {
        return 0;
    }
    return MsTx::Instance().MstxRangeAdd(std::string(message));
}

static void MstxRangeEnd(MstxRangeId id)
{
    return MsTx::Instance().MstxRangeEnd(id);
}

extern "C" __attribute__((visibility("default"))) int InitInjectionMstx(
    MstxGetModuleFuncTableFunc getFuncTable)
{
    if (getFuncTable == nullptr) {
        return MSTX_FAIL;
    }
    unsigned int outSize = 0;
    MstxFuncTable outTable;
    int result = getFuncTable(MstxFuncModule::MSTX_API_MODULE_CORE, &outTable, &outSize);
    if (result == MSTX_FAIL || outTable == nullptr) {
        return MSTX_FAIL;
    }

    if (outSize > static_cast<unsigned int>(MstxImplCoreFuncId::MSTX_API_CORE_RANGE_START_A)) {
        *outTable[static_cast<int>(MstxImplCoreFuncId::MSTX_API_CORE_RANGE_START_A)] =
                reinterpret_cast<MstxFuncPointer>(MstxRangeStartA);
    }

    if (outSize > static_cast<unsigned int>(MstxImplCoreFuncId::MSTX_API_CORE_RANGE_END)) {
        *outTable[static_cast<int>(MstxImplCoreFuncId::MSTX_API_CORE_RANGE_END)] =
                reinterpret_cast<MstxFuncPointer>(MstxRangeEnd);
    }

    if (outSize > static_cast<unsigned int>(MstxImplCoreFuncId::MSTX_API_CORE_GET_TOOL_ID)) {
        *outTable[static_cast<int>(MstxImplCoreFuncId::MSTX_API_CORE_GET_TOOL_ID)] =
                reinterpret_cast<MstxFuncPointer>(MstxGetToolId);
    }

    return MSTX_SUCCESS;
}

drvError_t halGetSocVersion(uint32_t devId, char *socVersion, uint32_t len)
{
    PRINT_ENTER_INSTRUMENTOR;
    if (!g_isCtorDone) {
        DEBUG_LOG("HijackedCtor invoked in halGetSocVersion");
        HijackedCtor();
    }
    HijackedFuncOfHalGetSocVersion instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(devId, socVersion, len);
    }
    return instance.Call(devId, socVersion, len);
}

/*************** implementation of mstx api by msprof end ***************/
