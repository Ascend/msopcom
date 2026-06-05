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
#include <unordered_set>

#include "ascend_dump/HijackedFunc.h"
#include "ascendcl/AscendclOrigin.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "acl_rt_impl/HijackedFunc.h"
#include "core/FuncSelector.h"
#include "core/HijackedLayerManager.h"
#include "runtime.h"
#include "runtime/HijackedFunc.h"
#include "runtime/RuntimeOrigin.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/MsTx.h"
#include "utils/InjectLogger.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"

static bool g_isCtorDone = false;
namespace {
#define PRINT_ENTER_INSTRUMENTOR DEBUG_LOG("enter %s", __FUNCTION__)

void HijackedAscendclImplCtor()
{
    REGISTER_LIBRARY(AclRuntimeLibName());
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtSetDeviceImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtResetDeviceImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMallocImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMallocHostImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMallocHostWithCfgImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtFreeImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtFreeHostImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMemsetImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMemcpyImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMapMemImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtUnmapMemImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtIpcMemGetExportKeyImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtIpcMemImportByKeyImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtIpcMemCloseImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryLoadFromFileImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryGetFunctionImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsInitImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsAppendImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsParaUpdateImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsAppendPlaceHolderImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsGetPlaceHolderBufferImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsFinalizeImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtLaunchKernelImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtLaunchKernelV2Impl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtLaunchKernelWithConfigImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtKernelArgsInitByUserMemImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtMallocWithCfgImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclmdlRICaptureBeginImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclmdlRICaptureEndImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclmdlRIBindStreamImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclmdlRIUnbindStreamImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtLaunchKernelWithHostArgsImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryLoadFromDataImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtCreateBinaryImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtBinaryLoadImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtGetFunctionAttributeImpl);
    REGISTER_FUNCTION(AclRuntimeLibName(), aclrtRegisterCpuFuncImpl);
}

void __attribute__ ((constructor)) HijackedCtor()
{
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    REGISTER_LIBRARY("runtime");
    REGISTER_LIBRARY("ascend_dump");
    REGISTER_FUNCTION("runtime", rtSetDevice);
    REGISTER_FUNCTION("runtime", rtMalloc);
    REGISTER_FUNCTION("runtime", rtFree);
    REGISTER_FUNCTION("runtime", rtMapMem);
    REGISTER_FUNCTION("runtime", rtUnmapMem);
    REGISTER_FUNCTION("runtime", rtMemset);
    REGISTER_FUNCTION("runtime", rtMemcpy);
    REGISTER_FUNCTION("runtime", rtDevBinaryRegister);
    REGISTER_FUNCTION("runtime", rtRegisterAllKernel);
    REGISTER_FUNCTION("runtime", rtFunctionRegister);
    REGISTER_FUNCTION("runtime", rtKernelLaunch);
    REGISTER_FUNCTION("runtime", rtKernelLaunchWithHandleV2);
    REGISTER_FUNCTION("runtime", rtKernelLaunchWithFlagV2);
    REGISTER_FUNCTION("runtime", rtSetExceptionExtInfo);
    REGISTER_FUNCTION("runtime", rtKernelLaunchWithHandle);
    REGISTER_FUNCTION("runtime", rtIpcSetMemoryName);
    REGISTER_FUNCTION("runtime", rtIpcDestroyMemoryName);
    REGISTER_FUNCTION("runtime", rtIpcOpenMemory);
    REGISTER_FUNCTION("runtime", rtIpcCloseMemory);
    REGISTER_FUNCTION("runtime", rtDeviceReset);
    REGISTER_FUNCTION("runtime", rtModelBindStream);
    REGISTER_FUNCTION("runtime", rtGetL2CacheOffset);
    REGISTER_FUNCTION("runtime", rtCtxGetOverflowAddr);
    REGISTER_FUNCTION("runtime", rtDeviceSetLimit);
    REGISTER_FUNCTION("ascend_dump", AdumpGetDFXInfoAddrForDynamic);
    RuntimeOriginCtor();
    AscendclOriginCtor();
    AscendclImplOriginCtor();
    HijackedAscendclImplCtor();
    g_isCtorDone = true;
}

bool IsDevIdUsed(int32_t devId)
{
    static std::mutex mtx;  // 定义一个静态的互斥锁，用于保护共享资源的访问
    static std::unordered_set<int32_t> usedDevIds;  // 静态的无序集合，记录已出现过的devId

    std::lock_guard<std::mutex> guard(mtx);  // 加锁，确保多线程安全访问usedDevIds集合
    if (usedDevIds.find(devId) != usedDevIds.end()) {
        return true;
    }
    usedDevIds.insert(devId);
    return false;
}

}

rtError_t rtSetDevice(int32_t devId)
{
    HijackedFuncOfSetDevice instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__) || IsDevIdUsed(devId)) {
        return instance.OriginCall(devId);
    }
    return instance.Call(devId);
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

rtError_t rtMapMem(void *devPtr, size_t size, size_t offset, rtDrvMemHandle_t *handle, uint64_t flags)
{
    HijackedFuncOfMapMem instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(devPtr, size, offset, handle, flags);
    }
    return instance.Call(devPtr, size, offset, handle, flags);
}

rtError_t rtUnmapMem(void *devPtr)
{
    HijackedFuncOfUnmapMem instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(devPtr);
    }
    return instance.Call(devPtr);
}

rtError_t rtMemset(void *devPtr, uint64_t destMax, uint32_t val, uint64_t cnt)
{
    HijackedFuncOfMemset instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(devPtr, destMax, val, cnt);
    }
    return instance.Call(devPtr, destMax, val, cnt);
}

rtError_t rtMemcpy(void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind)
{
    HijackedFuncOfMemcpy instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(dst, destMax, src, cnt, kind);
    }
    return instance.Call(dst, destMax, src, cnt, kind);
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

rtError_t rtKernelLaunchWithHandleV2(void *hdl, const uint64_t tilingKey, uint32_t blockDim,
                                     rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm,
                                     const rtTaskCfgInfo_t *cfgInfo)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    return instance.Call(hdl, tilingKey, blockDim, argsInfo, smDesc, stm, cfgInfo);
}

rtError_t rtKernelLaunchWithHandle(void *hdl, const uint64_t tilingKey, uint32_t blockDim, rtArgsEx_t *argsInfo,
                                   rtSmDesc_t *smDesc, rtStream_t stm, const void* kernelInfo)
{
    PRINT_ENTER_INSTRUMENTOR;
    (void)kernelInfo;
    HijackedFuncOfKernelLaunchWithHandleV2 instance;
    return instance.Call(hdl, tilingKey, blockDim, argsInfo, smDesc, stm, nullptr);
}

rtError_t rtKernelLaunchWithFlagV2(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                                   rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags,
                                   const rtTaskCfgInfo_t *cfgInfo)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfKernelLaunchWithFlagV2 instance;
    return instance.Call(stubFunc, blockDim, argsInfo, smDesc, stm, flags, cfgInfo);
}

rtError_t rtSetExceptionExtInfo(const rtArgsSizeInfo_t *const sizeInfo)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfSetExceptionExtInfo instance;
    return instance.Call(sizeInfo);
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

using namespace MstxAPI;
/*************** implementation of mstx api by mssanitizer ***************/
namespace {

inline bool IsAddrInRange(const HostMemRecord &record, uint64_t thresholdAddr, uint64_t thresholdSize)
{
    uint64_t addr = record.dstAddr;
    uint64_t size = record.memSize;
    if (addr >= thresholdAddr && thresholdSize >= size && addr - thresholdAddr <= thresholdSize - size) {
        return true;
    }
    return false;
}

inline bool IsInvalidMemInfo(const HostMemRecord &record)
{
    uint64_t addr = record.dstAddr;
    if (record.type == MemOpType::MALLOC && (addr == 0U || record.memSize == 0U)) {
        return true;
    }
    if (record.type  == MemOpType::FREE && addr == 0U) {
        return true;
    }
    return false;
}

MstxDomainRegistration* MstxDomainCreateA(const char *domainName)
{
    return MsTx::Instance().MstxDomainCreateA(std::string(domainName));
}

MstxMemHeap* MstxMemHeapRegister(MstxDomainRegistration* domain, MstxMemHeapDesc const *desc)
{
    auto ret = MsTx::Instance().MstxMemHeapRegister(domain, desc);
    if (ret == nullptr) {
        return nullptr;
    }
    MstxMemHeapRangeDesc heapBasicDesc(*desc);
    HostMemRecord record{};
    record.type = MemOpType::MALLOC;
    record.infoSrc = MemInfoSrc::MSTX_HEAP;
    record.dstAddr = reinterpret_cast<uint64_t>(heapBasicDesc.rangeDesc.ptr);
    record.memSize = heapBasicDesc.rangeDesc.size;
    if (IsInvalidMemInfo(record)) {
        ERROR_LOG("The addr or size of heap is 0, addr:%lx size:%lu", record.dstAddr, record.memSize);
        return ret;
    }
    MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(record.dstAddr, record.infoSrc, record.memSize);
    PacketHead head = { PacketType::MEMORY_RECORD };
    LocalDevice::GetInstance(static_cast<int32_t>(heapBasicDesc.rangeDesc.deviceId)).Notify(Serialize(head, record));
    return ret;
}

void MstxMemHeapUnregister(MstxDomainRegistration* domain, MstxMemHeap *heap)
{
    MstxMemHeapRangeDesc desc{};
    if (!MsTx::Instance().MstxMemHeapUnregister(domain, heap, &desc)) {
        return;
    }
    HostMemRecord record{};
    record.type = MemOpType::FREE;
    record.infoSrc = MemInfoSrc::MSTX_HEAP;
    record.dstAddr = reinterpret_cast<uint64_t>(desc.rangeDesc.ptr);
    if (IsInvalidMemInfo(record)) {
        ERROR_LOG("The addr of regions is 0");
        return;
    }
    MemoryManage::Instance().CacheMemory<MemoryOpType::FREE>(record.dstAddr, record.infoSrc);
    PacketHead head = { PacketType::MEMORY_RECORD };
    LocalDevice::GetInstance(static_cast<int32_t>(desc.rangeDesc.deviceId)).Notify(Serialize(head, record));
}

void MstxMemRegionsRegister(MstxDomainRegistration* domain, MstxMemRegionsRegisterBatch const *desc)
{
    auto ret = MsTx::Instance().MstxMemRegionsRegister(domain, desc);
    if (ret.success) {
        const auto *rangeDesc = static_cast<const MstxMemVirtualRangeDesc*>(desc->regionDescArray);
        PacketHead head = { PacketType::MEMORY_RECORD };
        HostMemRecord record{};
        record.infoSrc = MemInfoSrc::MSTX_REGION;
        record.rootAddr = ret.addr;
        for (size_t i = 0; i < desc->regionCount; ++i) {
            record.type = MemOpType::MALLOC;
            record.dstAddr = reinterpret_cast<uint64_t>(rangeDesc[i].ptr);
            record.memSize = rangeDesc[i].size;
            if (IsInvalidMemInfo(record)) {
                ERROR_LOG("The addr or size of regions is 0, addr:%lx size:%lu", record.dstAddr, record.memSize);
                continue;
            }
            if (desc->heap == nullptr || IsAddrInRange(record, ret.addr, ret.size)) {
                MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(record.dstAddr,
                    record.infoSrc, record.memSize);
                LocalDevice::GetInstance(static_cast<int32_t>(rangeDesc[i].deviceId)).Notify(Serialize(head, record));
                record.type = MemOpType::STORE;
                LocalDevice::GetInstance(static_cast<int32_t>(rangeDesc[i].deviceId)).Notify(Serialize(head, record));
            } else {
                ERROR_LOG("the regions exceed the memHeap range, region addr:0x%lx size:%lu,"
                          " memHeap addr:0x%lx size:%lu", record.dstAddr, record.memSize, ret.addr, ret.size);
            }
        }
    }
}

void MstxMemRegionsUnregister(MstxDomainRegistration* domain, MstxMemRegionsUnregisterBatch const *desc)
{
    std::vector<MstxMemVirtualRangeDesc> rangeDescVec;
    if (!MsTx::Instance().MstxMemRegionsUnregister(domain, desc, rangeDescVec)) {
        return;
    }
    PacketHead head = { PacketType::MEMORY_RECORD };
    HostMemRecord record{};
    record.type = MemOpType::FREE;
    record.infoSrc = MemInfoSrc::MSTX_REGION;
    for (auto const &range: rangeDescVec) {
        record.dstAddr = reinterpret_cast<uint64_t>(range.ptr);
        record.memSize = range.size;
        if (IsInvalidMemInfo(record)) {
            ERROR_LOG("The addr of regions is 0");
            continue;
        }
        MemoryManage::Instance().CacheMemory<MemoryOpType::FREE>(record.dstAddr, record.infoSrc, record.memSize);
        LocalDevice::GetInstance(static_cast<int32_t>(range.deviceId)).Notify(Serialize(head, record));
    }

    record.memSize = 0U;
    for (size_t i = 0; i < desc->refCount; ++i) {
        if (desc->refArray[i].refType == MstxMemRegionRefType::MSTX_MEM_REGION_REF_TYPE_POINTER) {
            record.dstAddr = reinterpret_cast<uint64_t>(desc->refArray[i].pointer);
            if (IsInvalidMemInfo(record)) {
                ERROR_LOG("The addr of regions is 0");
                continue;
            }
            MemoryManage::Instance().CacheMemory<MemoryOpType::FREE>(record.dstAddr, record.infoSrc, record.memSize);
            LocalDevice::Local().Notify(Serialize(head, record));
        }
    }
}

void MstxMemPermissionsAssign(MstxDomainRegistration *domain, MstxMemPermissionsAssignBatch const *desc)
{
    std::vector<MstxMemVirtualRangeDesc> rangeDescVec;
    if (!MsTx::Instance().MstxMemPermissionsAssign(domain, desc, rangeDescVec)) {
        ERROR_LOG("Query mem region description by mem region reference failed");
        return;
    }

    PacketHead head = { PacketType::MEM_REGION_PERMISSION };
    for (std::size_t i = 0; i < desc->regionCount; ++i) {
        // rangeDescVec must have same size to desc->regionDescArray
        MstxMemVirtualRangeDesc const &range = rangeDescVec[i];
        MstxMemPermissionsAssignRegionsDesc const&permission = desc->regionDescArray[i];
        DEBUG_LOG("Report mem region assigned permissions. addr:%p, size:%lu", range.ptr, range.size);
        MemRegionPermissionDesc desc{};
        desc.addr = reinterpret_cast<uint64_t>(range.ptr);
        desc.size = range.size;
        desc.deviceId = range.deviceId;
        desc.flags = permission.flags;
        MemoryManage::Instance().SetPermission(desc.addr, desc.size, desc.flags);
        LocalDevice::GetInstance(static_cast<int32_t>(range.deviceId)).Notify(Serialize(head, desc));
    }
}

void MstxGetToolId(uint64_t *id)
{
    if (id == nullptr) {
        return;
    }
    *id = MSTX_TOOL_MSSANITIZER_ID;
}
}

template<typename FuncIdEnum>
using InjectionMap = std::vector<std::pair<FuncIdEnum, MstxFuncPointer>>;

// 各模块的映射表
static const InjectionMap<MstxImplCoreFuncId> CoreInjections = {
    {MstxImplCoreFuncId::MSTX_API_CORE_GET_TOOL_ID, reinterpret_cast<MstxFuncPointer>(MstxGetToolId)}
};

static const InjectionMap<MstxImplCoreDomainFuncId> DomainInjections = {
    {MstxImplCoreDomainFuncId::MSTX_API_CORE2_DOMAIN_CREATE_A, reinterpret_cast<MstxFuncPointer>(MstxDomainCreateA)}
};

static const InjectionMap<MstxImplCoreMemFuncId> MemInjections = {
    {MstxImplCoreMemFuncId::MSTX_API_CORE_MEMHEAP_REGISTER,
        reinterpret_cast<MstxFuncPointer>(MstxMemHeapRegister)},
    {MstxImplCoreMemFuncId::MSTX_API_CORE_MEMHEAP_UNREGISTER,
        reinterpret_cast<MstxFuncPointer>(MstxMemHeapUnregister)},
    {MstxImplCoreMemFuncId::MSTX_API_CORE_MEM_REGIONS_REGISTER,
        reinterpret_cast<MstxFuncPointer>(MstxMemRegionsRegister)},
    {MstxImplCoreMemFuncId::MSTX_API_CORE_MEM_REGIONS_UNREGISTER,
        reinterpret_cast<MstxFuncPointer>(MstxMemRegionsUnregister)},
    {MstxImplCoreMemFuncId::MSTX_API_CORE_MEM_PERMISSIONS_ASSIGN,
        reinterpret_cast<MstxFuncPointer>(MstxMemPermissionsAssign)},
};

template<typename FuncIdEnum>
static bool InitInjectionGeneric(const MstxFuncModule &module,
    const InjectionMap<FuncIdEnum> &funcMap, MstxGetModuleFuncTableFunc getFuncTable)
{
    bool injectResult = false;
    unsigned int outSize = 0;
    MstxFuncTable outTable;
    int funcResult = getFuncTable(module, &outTable, &outSize);
    if (funcResult == MSTX_SUCCESS && outTable != nullptr) {
        for (const auto &item : funcMap) {
            if (outSize > static_cast<size_t>(item.first)) {
                *outTable[static_cast<int>(item.first)] = item.second;
                injectResult = true;
            }
        }
    }
    return injectResult;
}

extern "C" __attribute__((visibility("default"))) int InitInjectionMstx(MstxGetModuleFuncTableFunc getFuncTable)
{
    if (getFuncTable == nullptr) {
        return MSTX_FAIL;
    }

    int injectResult = MSTX_FAIL;
    if (InitInjectionGeneric(MstxFuncModule::MSTX_API_MODULE_CORE, CoreInjections, getFuncTable)) {
        injectResult = MSTX_SUCCESS;
    }

    if (InitInjectionGeneric(MstxFuncModule::MSTX_API_MODULE_CORE_MEM, MemInjections, getFuncTable)) {
        injectResult = MSTX_SUCCESS;
    }

    if (InitInjectionGeneric(MstxFuncModule::MSTX_API_MODULE_CORE_DOMAIN, DomainInjections, getFuncTable)) {
        injectResult = MSTX_SUCCESS;
    }

    return injectResult;
}

extern "C" __attribute__((visibility("default"))) void __sanitizer_report_malloc(void *ptr, uint64_t size)
{
    uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    ReportMalloc(addr, size, MemInfoSrc::MANUAL);
    // 在对算子进行检测时不关心实际的内存申请和释放，手动上报的 ReportMalloc 和
    // ReportFree 用于上报在算子设计中可访问的 GM 范围。为了防止手动上报内存结合检测未
    // 使用内存功能同时使用时会产生误报，通过模拟对手动分配内存的 store 操作使该片内
    // 存变为 DEFINED 状态。同时为了兼容日后的 read-before-write 和 write-before-read
    // 检测，依次使用 store 和 load 操作将内存变为可读可写状态。状态变化示意如下：
    // NOACCESS -malloc-> UNDEFINED -store-> DEFINED(R) -load-> DEFINED(RW)
    ReportMemset(addr, size, MemInfoSrc::MANUAL);
}

extern "C" __attribute__((visibility("default"))) void __sanitizer_report_free(void *ptr)
{
    uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    ReportFree(addr, MemInfoSrc::MANUAL);
}

/*************** implementation of mstx api by mssanitizer end ***************/

rtError_t rtIpcSetMemoryName(const void *ptr, uint64_t byteCount, char *name, uint32_t len)
{
    HijackedFuncOfIpcSetMemoryName instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(ptr, byteCount, name, len);
    }
    return instance.Call(ptr, byteCount, name, len);
}

rtError_t rtIpcDestroyMemoryName(const char *name)
{
    HijackedFuncOfIpcDestroyMemoryName instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(name);
    }
    return instance.Call(name);
}


rtError_t rtIpcOpenMemory(void **ptr, const char *name)
{
    HijackedFuncOfIpcOpenMemory instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(ptr, name);
    }
    return instance.Call(ptr, name);
}

rtError_t rtIpcCloseMemory(const void *ptr)
{
    HijackedFuncOfIpcCloseMemory instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(ptr);
    }
    return instance.Call(ptr);
}

rtError_t rtDeviceReset(int32_t devId)
{
    HijackedFuncOfDeviceReset instance;
    if (HijackedLayerManager::Instance().ParentInCallStack(__func__)) {
        return instance.OriginCall(devId);
    }
    return instance.Call(devId);
}

rtError_t rtModelBindStream(rtModel_t mdl, rtStream_t stm, uint32_t flag)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfModelBindStream instance;
    return instance.Call(mdl, stm, flag);
}

rtError_t rtCtxGetOverflowAddr(void **overflowAddr)
{
    HijackedFuncOfCtxGetOverflowAddr instance;
    return instance.Call(overflowAddr);
}

aclError aclrtSetDeviceImpl(int32_t deviceId)
{
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtSetDeviceImpl instance;
    if (IsDevIdUsed(deviceId)) {
        return instance.OriginCall(deviceId);
    }
    return instance.Call(deviceId);
}

aclError aclrtResetDeviceImpl(int32_t deviceId)
{
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtResetDeviceImpl instance;
    return instance.Call(deviceId);
}

aclError aclrtMallocImpl(void **devPtr, size_t size, aclrtMemMallocPolicy policy)
{
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtMallocImpl instance;
    return instance.Call(devPtr, size, policy);
}

aclError aclrtFreeImpl(void *devPtr)
{
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtFreeImpl instance;
    return instance.Call(devPtr);
}

aclError aclrtMemsetImpl(void *devPtr, size_t maxCount, int32_t value, size_t count)
{
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtMemsetImpl instance;
    return instance.Call(devPtr, maxCount, value, count);
}

aclError aclrtMemcpyImpl(void *dst, size_t destMax, const void *src,
                         size_t count, aclrtMemcpyKind kind)
{
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtMemcpyImpl instance;
    return instance.Call(dst, destMax, src, count, kind);
}

aclError aclrtMapMemImpl(void *virPtr, size_t size, size_t offset,
                         aclrtDrvMemHandle handle, uint64_t flags)
{
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtMapMemImpl instance;
    return instance.Call(virPtr, size, offset, handle, flags);
}

aclError aclrtUnmapMemImpl(void *virPtr)
{
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtUnmapMemImpl instance;
    return instance.Call(virPtr);
}

aclError aclrtIpcMemGetExportKeyImpl(void *devPtr, size_t size, char *key,
                                     size_t len, uint64_t flag)
{
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtIpcMemGetExportKeyImpl instance;
    return instance.Call(devPtr, size, key, len, flag);
}

aclError aclrtIpcMemImportByKeyImpl(void **devPtr, const char *key, uint64_t flag)
{
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtIpcMemImportByKeyImpl instance;
    return instance.Call(devPtr, key, flag);
}

aclError aclrtIpcMemCloseImpl(const char *key)
{
    LayerGuard guard(HijackedLayerManager::Instance(), __func__);
    HijackedFuncOfAclrtIpcMemCloseImpl instance;
    return instance.Call(key);
}

aclError aclrtBinaryLoadFromFileImpl(const char *binPath, aclrtBinaryLoadOptions *options,
                                     aclrtBinHandle *binHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtBinaryLoadFromFileImpl instance;
    return instance.Call(binPath, options, binHandle);
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

aclError aclrtBinaryLoadImpl(const aclrtBinary binary, aclrtBinHandle *binHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtBinaryLoadImpl instance;
    return instance.Call(binary, binHandle);
}

aclError aclrtBinaryLoadFromDataImpl(const void *data, size_t length,
                                     const aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtBinaryLoadFromDataImpl instance;
    return instance.Call(data, length, options, binHandle);
}

aclrtBinary aclrtCreateBinaryImpl(const void *data, size_t dataLen)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtCreateBinaryImpl instance;
    return instance.Call(data, dataLen);
}

aclError aclrtBinaryGetFunctionImpl(const aclrtBinHandle binHandle, const char *kernelName,
                                    aclrtFuncHandle *funcHandle)
{
    HijackedFuncOfAclrtBinaryGetFunctionImpl instance;
    return instance.Call(binHandle, kernelName, funcHandle);
}

aclError aclrtBinaryGetFunctionByEntryImpl(aclrtBinHandle binHandle, uint64_t funcEntry,
                                           aclrtFuncHandle *funcHandle)
{
    HijackedFuncOfAclrtBinaryGetFunctionByEntryImpl instance;
    return instance.Call(binHandle, funcEntry, funcHandle);
}

aclError aclrtKernelArgsInitImpl(aclrtFuncHandle funcHandle, aclrtArgsHandle *argsHandle)
{
    HijackedFuncOfAclrtKernelArgsInitImpl instance;
    return instance.Call(funcHandle, argsHandle);
}

aclError aclrtRegisterCpuFuncImpl(const aclrtBinHandle binHandle, const char *funcName, const char *kernelName, aclrtFuncHandle *funcHandle)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclrtRegisterCpuFuncImpl instance;
    return instance.Call(binHandle, funcName, kernelName, funcHandle);
}

aclError aclrtKernelArgsFinalizeImpl(aclrtArgsHandle argsHandle)
{
    HijackedFuncOfAclrtKernelArgsFinalizeImpl instance;
    return instance.Call(argsHandle);
}

aclError aclrtKernelArgsAppendImpl(aclrtArgsHandle argsHandle, void *param, size_t paramSize, aclrtParamHandle *paramHandle)
{
    HijackedFuncOfAclrtKernelArgsAppendImpl instance;
    return instance.Call(argsHandle, param, paramSize, paramHandle);
}

aclError aclrtKernelArgsParaUpdateImpl(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle, void *param, size_t paramSize)
{
    HijackedFuncOfAclrtKernelArgsParaUpdateImpl instance;
    return instance.Call(argsHandle, paramHandle, param, paramSize);
}

aclError aclrtKernelArgsAppendPlaceHolderImpl(aclrtArgsHandle argsHandle, aclrtParamHandle *paramHandle)
{
    HijackedFuncOfAclrtKernelArgsAppendPlaceHolderImpl instance;
    return instance.Call(argsHandle, paramHandle);
}

aclError aclrtKernelArgsGetPlaceHolderBufferImpl(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle,
                                                 size_t dataSize, void **bufferAddr)
{
    HijackedFuncOfAclrtKernelArgsGetPlaceHolderBufferImpl instance;
    return instance.Call(argsHandle, paramHandle, dataSize, bufferAddr);
}

aclError aclrtLaunchKernelWithConfigImpl(aclrtFuncHandle funcHandle, uint32_t blockDim,
                                         aclrtStream stream, aclrtLaunchKernelCfg *cfg,
                                         aclrtArgsHandle argsHandle, void *reserve)
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

aclError aclmdlRICaptureBeginImpl(aclrtStream stream, aclmdlRICaptureMode mode)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclmdlRICaptureBeginImpl instance;
    return instance.Call(stream, mode);
}

aclError aclmdlRICaptureEndImpl(aclrtStream stream, aclmdlRI *modelRI)
{
    PRINT_ENTER_INSTRUMENTOR;
    HijackedFuncOfAclmdlRICaptureEndImpl instance;
    return instance.Call(stream, modelRI);
}

aclError aclmdlRIBindStreamImpl(aclmdlRI modelRI, aclrtStream stream, uint32_t flag)
{
    HijackedFuncOfAclmdlRIBindStreamImpl instance;
    return instance.Call(modelRI, stream, flag);
}

aclError aclmdlRIUnbindStreamImpl(aclmdlRI modelRI, aclrtStream stream)
{
    HijackedFuncOfAclmdlRIUnbindStreamImpl instance;
    return instance.Call(modelRI, stream);
}
