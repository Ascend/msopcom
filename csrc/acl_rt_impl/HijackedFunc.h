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

#include <functional>
#include "acl.h"
#include "core/HijackedFuncTemplate.h"
#include "runtime/inject_helpers/ArgsContext.h"
#include "runtime/inject_helpers/FuncContext.h"
#include "runtime/inject_helpers/LaunchContext.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "acl_rt_impl/AclRuntimeConfig.h"
#include "runtime/inject_helpers/ProfConfig.h"

class AclErrorTag;
 
template <>
struct EmptyFuncError<TaggedType<aclError, AclErrorTag>> {
    static constexpr aclError VALUE = ACL_ERROR_INTERNAL_ERROR;
};

template <typename ReturnType, typename... Args>
auto AscendclImpHijackedType(ReturnType (*func)(Args...))
    -> HijackedFunc<TaggedType<ReturnType, AclErrorTag>, Args...>
{
    return HijackedFuncHelperTagged<AclErrorTag>(func);
}

class HijackedFuncOfAclrtSetDeviceImpl : public decltype(AscendclImpHijackedType(&aclrtSetDeviceImpl)) {
public:
    explicit HijackedFuncOfAclrtSetDeviceImpl();
    void Pre(int32_t devId) override;
    aclError Call(int32_t devId) override;
    aclError Post(aclError ret) override;

private:
    int32_t devId_{0};
};

class HijackedFuncOfAclrtResetDeviceImpl : public decltype(AscendclImpHijackedType(&aclrtResetDeviceImpl)) {
public:
    explicit HijackedFuncOfAclrtResetDeviceImpl();
    void Pre(int32_t devId) override;
    aclError Call(int32_t devId) override;
    aclError Post(aclError ret) override;

private:
    int32_t devId_{0};
};

class HijackedFuncOfAclrtMallocImpl : public decltype(AscendclImpHijackedType(&aclrtMallocImpl)) {
public:
    explicit HijackedFuncOfAclrtMallocImpl();
    ~HijackedFuncOfAclrtMallocImpl() override = default;
    void Pre(void **devPtr, size_t size, aclrtMemMallocPolicy policy) override;
    aclError Call(void **devPtr, size_t size, aclrtMemMallocPolicy policy) override;
    aclError Post(aclError ret) override;

private:
    void **devPtr_{nullptr};
    size_t size_{0};
    aclrtMemMallocPolicy policy_ {aclrtMemMallocPolicy::ACL_MEM_MALLOC_HUGE_FIRST};
    size_t actualSize_{0};
};
class HijackedFuncOfAclrtMallocWithCfgImpl : public decltype(AscendclImpHijackedType(&aclrtMallocWithCfgImpl)) {
public:
    explicit HijackedFuncOfAclrtMallocWithCfgImpl();
    ~HijackedFuncOfAclrtMallocWithCfgImpl() override = default;
    void Pre(void **devPtr, size_t size, aclrtMemMallocPolicy policy, aclrtMallocConfig *cfg) override;
    aclError Call(void **devPtr, size_t size, aclrtMemMallocPolicy policy, aclrtMallocConfig *cfg) override;
    aclError Post(aclError ret) override;

private:
    void **devPtr_{nullptr};
    size_t size_{0};
    aclrtMemMallocPolicy policy_ {aclrtMemMallocPolicy::ACL_MEM_MALLOC_HUGE_FIRST};
    size_t actualSize_{0};
};

class HijackedFuncOfAclrtMallocCachedImpl : public decltype(AscendclImpHijackedType(&aclrtMallocCachedImpl)) {
public:
    explicit HijackedFuncOfAclrtMallocCachedImpl();
    ~HijackedFuncOfAclrtMallocCachedImpl() override = default;
    void Pre(void **devPtr, size_t size, aclrtMemMallocPolicy policy) override;
    aclError Call(void **devPtr, size_t size, aclrtMemMallocPolicy policy) override;
    aclError Post(aclError ret) override;

private:
    void **devPtr_{nullptr};
    size_t size_{0};
    aclrtMemMallocPolicy policy_ {aclrtMemMallocPolicy::ACL_MEM_MALLOC_HUGE_FIRST};
    size_t actualSize_{0};
};

class HijackedFuncOfAclrtMallocHostImpl : public decltype(AscendclImpHijackedType(&aclrtMallocHostImpl)) {
public:
    explicit HijackedFuncOfAclrtMallocHostImpl();
    ~HijackedFuncOfAclrtMallocHostImpl() override = default;
    void Pre(void **hostPtr, size_t size) override;
    aclError Post(aclError ret) override;

private:
    void **hostPtr_{nullptr};
    size_t size_{0};
};

class HijackedFuncOfAclrtMallocHostWithCfgImpl : public decltype(AscendclImpHijackedType(&aclrtMallocHostWithCfgImpl)) {
public:
    explicit HijackedFuncOfAclrtMallocHostWithCfgImpl();
    ~HijackedFuncOfAclrtMallocHostWithCfgImpl() override = default;
    void Pre(void **hostPtr, size_t size, aclrtMallocConfig *cfg) override;
    aclError Post(aclError ret) override;

private:
    void **hostPtr_{nullptr};
    size_t size_{0};
};

class HijackedFuncOfAclrtFreeImpl : public decltype(AscendclImpHijackedType(&aclrtFreeImpl)) {
public:
    explicit HijackedFuncOfAclrtFreeImpl();
    ~HijackedFuncOfAclrtFreeImpl() override = default;
    void Pre(void *devPtr) override;
    aclError Call(void *devPtr) override;

private:
    void *devPtr_{nullptr};
    void *actualPtr_{nullptr};
};

class HijackedFuncOfAclrtFreeHostImpl : public decltype(AscendclImpHijackedType(&aclrtFreeHostImpl)) {
public:
    explicit HijackedFuncOfAclrtFreeHostImpl();
    ~HijackedFuncOfAclrtFreeHostImpl() override = default;
    void Pre(void *hostPtr) override;
};

class HijackedFuncOfAclrtMemsetImpl : public decltype(AscendclImpHijackedType(&aclrtMemsetImpl)) {
public:
    explicit HijackedFuncOfAclrtMemsetImpl();
    ~HijackedFuncOfAclrtMemsetImpl() override = default;
    void Pre(void *devPtr, size_t maxCount, int32_t value, size_t count) override;
};

class HijackedFuncOfAclrtMemsetAsyncImpl : public decltype(AscendclImpHijackedType(&aclrtMemsetAsyncImpl)) {
public:
    explicit HijackedFuncOfAclrtMemsetAsyncImpl();
    ~HijackedFuncOfAclrtMemsetAsyncImpl() override = default;
    void Pre(void *devPtr, size_t maxCount, int32_t value, size_t count, aclrtStream stream) override;
};

class HijackedFuncOfAclrtMemcpyImpl : public decltype(AscendclImpHijackedType(&aclrtMemcpyImpl)) {
public:
    explicit HijackedFuncOfAclrtMemcpyImpl();
    ~HijackedFuncOfAclrtMemcpyImpl() override = default;
    void Pre(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind) override;
};

class HijackedFuncOfAclrtMemcpyAsyncImpl : public decltype(AscendclImpHijackedType(&aclrtMemcpyAsyncImpl)) {
public:
    explicit HijackedFuncOfAclrtMemcpyAsyncImpl();
    ~HijackedFuncOfAclrtMemcpyAsyncImpl() override = default;
    void Pre(void *dst, size_t destMax, const void *src, size_t count,
             aclrtMemcpyKind kind, aclrtStream stream) override;
};

class HijackedFuncOfAclrtMemcpy2dImpl : public decltype(AscendclImpHijackedType(&aclrtMemcpy2dImpl)) {
public:
    explicit HijackedFuncOfAclrtMemcpy2dImpl();
    ~HijackedFuncOfAclrtMemcpy2dImpl() override = default;
    void Pre(void *dst, size_t dpitch, const void *src, size_t spitch,
             size_t width, size_t height, aclrtMemcpyKind kind) override;
};

class HijackedFuncOfAclrtMemcpy2dAsyncImpl : public decltype(AscendclImpHijackedType(&aclrtMemcpy2dAsyncImpl)) {
public:
    explicit HijackedFuncOfAclrtMemcpy2dAsyncImpl();
    ~HijackedFuncOfAclrtMemcpy2dAsyncImpl() override = default;
    void Pre(void *dst, size_t dpitch, const void *src, size_t spitch,
             size_t width, size_t height, aclrtMemcpyKind kind, aclrtStream stream) override;
};

class HijackedFuncOfAclrtMapMemImpl : public decltype(AscendclImpHijackedType(&aclrtMapMemImpl)) {
public:
    explicit HijackedFuncOfAclrtMapMemImpl();
    ~HijackedFuncOfAclrtMapMemImpl() override = default;
    void Pre(void *virPtr, size_t size, size_t offset, aclrtDrvMemHandle handle, uint64_t flags) override;
    aclError Post(aclError ret) override;
private:
    void *virPtr_{nullptr};
    uint64_t size_{0};
};

class HijackedFuncOfAclrtIpcMemGetExportKeyImpl
    : public decltype(AscendclImpHijackedType(&aclrtIpcMemGetExportKeyImpl)) {
public:
    explicit HijackedFuncOfAclrtIpcMemGetExportKeyImpl();
    void Pre(void *devPtr, size_t size, char *key, size_t len, uint64_t flag) override;
    aclError Call(void *devPtr, size_t size, char *key, size_t len, uint64_t flag) override;
    aclError Post(aclError ret) override;

private:
    void *devPtr_{};
    size_t size_{};
    char *key_{};
    size_t len_{};
};

class HijackedFuncOfAclrtIpcMemImportByKeyImpl
    : public decltype(AscendclImpHijackedType(&aclrtIpcMemImportByKeyImpl)) {
public:
    explicit HijackedFuncOfAclrtIpcMemImportByKeyImpl();
    void Pre(void **devPtr, const char *key, uint64_t flag) override;
    aclError Call(void **devPtr, const char *key, uint64_t flag) override;
    aclError Post(aclError ret) override;

private:
    void **devPtr_{};
    const char *key_{};
};

class HijackedFuncOfAclrtIpcMemCloseImpl
    : public decltype(AscendclImpHijackedType(&aclrtIpcMemCloseImpl)) {
public:
    explicit HijackedFuncOfAclrtIpcMemCloseImpl();
    void Pre(const char *key) override;
    aclError Post(aclError ret) override;

private:
    const char *key_{};
};

class HijackedFuncOfAclrtUnmapMemImpl : public decltype(AscendclImpHijackedType(&aclrtUnmapMemImpl)) {
public:
    explicit HijackedFuncOfAclrtUnmapMemImpl();
    ~HijackedFuncOfAclrtUnmapMemImpl() override = default;
    void Pre(void* virPtr) override;
};

class HijackedFuncOfAclrtBinaryLoadFromFileImpl: public decltype(AscendclImpHijackedType(&aclrtBinaryLoadFromFileImpl)) {
public:
    explicit HijackedFuncOfAclrtBinaryLoadFromFileImpl();
    void Pre(const char* binPath, aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle) override;

    aclError Post(aclError ret) override;
private:
    const char *binPath_{nullptr};
    aclrtBinHandle *binHandle_{nullptr};
    aclrtBinaryLoadOptions *options_{nullptr};
};

class HijackedFuncOfAclrtBinaryLoadImpl: public decltype(AscendclImpHijackedType(&aclrtBinaryLoadImpl)) {
public:
    explicit HijackedFuncOfAclrtBinaryLoadImpl();

    void Pre(const aclrtBinary binary, aclrtBinHandle *binHandle) override;

    aclError Post(aclError ret) override;
private:
    aclrtBinary bin_{nullptr};
    aclrtBinHandle *binHandle_{nullptr};
};

class HijackedFuncOfAclrtBinaryLoadFromDataImpl: public decltype(AscendclImpHijackedType(&aclrtBinaryLoadFromDataImpl)) {
public:
    explicit HijackedFuncOfAclrtBinaryLoadFromDataImpl();

    void Pre(const void *data, size_t length, const aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle) override;

    aclError Post(aclError ret) override;
private:
    const void * bin_{nullptr};
    aclrtBinHandle *binHandle_{nullptr};
    aclrtBinaryLoadOptions *options_{nullptr};
    size_t length_ {0};
};

class HijackedFuncOfAclrtCreateBinaryImpl: public decltype(HijackedFuncHelper(&aclrtCreateBinaryImpl)) {
public:
    explicit HijackedFuncOfAclrtCreateBinaryImpl();

    void Pre(const void *data, size_t dataLen) override;

    aclrtBinary Post(aclrtBinary bin) override;
private:
    const char *data_{nullptr};
    size_t dataLen_{0};
};

class HijackedFuncOfAclrtBinaryGetFunctionImpl: public decltype(
    AscendclImpHijackedType(&aclrtBinaryGetFunctionImpl)) {
public:
    explicit HijackedFuncOfAclrtBinaryGetFunctionImpl();

    void Pre(const aclrtBinHandle binHandle,
             const char *kernelName, aclrtFuncHandle *funcHandle) override;

    aclError Post(aclError ret) override;
private:
    const char *kernelName_{nullptr};
    aclrtFuncHandle *funcHandle_{nullptr};
    aclrtBinHandle binHandle_{nullptr};
};


class HijackedFuncOfAclrtBinaryGetFunctionByEntryImpl: public decltype(
    AscendclImpHijackedType(&aclrtBinaryGetFunctionByEntryImpl)) {
public:
    explicit HijackedFuncOfAclrtBinaryGetFunctionByEntryImpl();

    void Pre(aclrtBinHandle binHandle, uint64_t funcEntry, aclrtFuncHandle *funcHandle) override;

    aclError Post(aclError ret) override;
private:
    uint64_t funcEntry_{0};
    aclrtFuncHandle *funcHandle_{nullptr};
    aclrtBinHandle binHandle_{nullptr};
};

class HijackedFuncOfAclrtRegisterCpuFuncImpl: public decltype(
    AscendclImpHijackedType(&aclrtRegisterCpuFuncImpl)) {
public:
    explicit HijackedFuncOfAclrtRegisterCpuFuncImpl();

    void Pre(const aclrtBinHandle binHandle, const char *funcName, const char *kernelName, aclrtFuncHandle *funcHandle) override;

    aclError Post(aclError ret) override;
private:
    const char *funcName_{nullptr};
    const char *kernelName_{nullptr};
    aclrtFuncHandle *funcHandle_{nullptr};
    aclrtBinHandle binHandle_{nullptr};
};

class HijackedFuncOfAclrtKernelArgsInitImpl: public decltype(
    AscendclImpHijackedType(&aclrtKernelArgsInitImpl)) {
public:
    explicit HijackedFuncOfAclrtKernelArgsInitImpl();

    void Pre(aclrtFuncHandle funcHandle, aclrtArgsHandle *argsHandle) override;

    aclError Post(aclError ret) override;
private:
    aclrtFuncHandle funcHandle_{nullptr};
    aclrtArgsHandle *argsHandle_{nullptr};
};

class HijackedFuncOfAclrtKernelArgsAppendImpl: public decltype(
    AscendclImpHijackedType(&aclrtKernelArgsAppendImpl)) {
public:
    explicit HijackedFuncOfAclrtKernelArgsAppendImpl();

    void Pre(aclrtArgsHandle argsHandle, void *param, size_t paramSize, aclrtParamHandle *paramHandle) override;

    aclError Post(aclError ret) override;
private:
    aclrtArgsHandle argsHandle_{nullptr};
    void *param_{nullptr};
    size_t paramSize_{0};
    aclrtParamHandle *paramHandle_{nullptr};
};

class HijackedFuncOfAclrtKernelArgsAppendPlaceHolderImpl: public decltype(
    AscendclImpHijackedType(&aclrtKernelArgsAppendPlaceHolderImpl)) {
public:
    explicit HijackedFuncOfAclrtKernelArgsAppendPlaceHolderImpl();

    void Pre(aclrtArgsHandle argsHandle, aclrtParamHandle *paramHandle) override;

    aclError Post(aclError ret) override;
private:
    aclrtArgsHandle argsHandle_{nullptr};
    aclrtParamHandle *paramHandle_{nullptr};
};

class HijackedFuncOfAclrtKernelArgsGetPlaceHolderBufferImpl: public decltype(
    AscendclImpHijackedType(&aclrtKernelArgsGetPlaceHolderBufferImpl)) {
public:
    explicit HijackedFuncOfAclrtKernelArgsGetPlaceHolderBufferImpl();

    void Pre(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle,
             size_t dataSize, void **bufferAddr) override;

    aclError Post(aclError ret) override;
private:
    aclrtArgsHandle argsHandle_{nullptr};
    aclrtParamHandle paramHandle_{nullptr};
    void **bufferAddr_{nullptr};
    size_t dataSize_{0};
};

class HijackedFuncOfAclrtKernelArgsParaUpdateImpl: public decltype(
    AscendclImpHijackedType(&aclrtKernelArgsParaUpdateImpl)) {
public:
    explicit HijackedFuncOfAclrtKernelArgsParaUpdateImpl();

    void Pre(aclrtArgsHandle argsHandle, aclrtParamHandle paramHandle,
             void *param, size_t paramSize) override;

    aclError Post(aclError ret) override;
private:
    aclrtArgsHandle argsHandle_{nullptr};
    aclrtParamHandle paramHandle_{nullptr};
    void *param_{nullptr};
    size_t paramSize_{0};
};

class HijackedFuncOfAclrtKernelArgsFinalizeImpl: public decltype(
    AscendclImpHijackedType(&aclrtKernelArgsFinalizeImpl)) {
public:
    explicit HijackedFuncOfAclrtKernelArgsFinalizeImpl();

    void Pre(aclrtArgsHandle argsHandle) override;

    aclError Post(aclError ret) override;
private:
    aclrtArgsHandle argsHandle_{nullptr};
};

class HijackedFuncOfAclrtGetDeviceImpl : public decltype(AscendclImpHijackedType(&aclrtGetDeviceImpl)) {
public:
    explicit HijackedFuncOfAclrtGetDeviceImpl();
    aclError Call(int32_t *devId) override;
};

class HijackedFuncOfAclrtCreateContextImpl : public decltype(AscendclImpHijackedType(&aclrtCreateContextImpl)) {
public:
    explicit HijackedFuncOfAclrtCreateContextImpl();
    aclError Call(aclrtContext *context, int32_t deviceId) override;
};

class HijackedFuncOfAclrtQueryDeviceStatusImpl : public decltype(AscendclImpHijackedType(&aclrtQueryDeviceStatusImpl)) {
public:
    explicit HijackedFuncOfAclrtQueryDeviceStatusImpl();
    aclError Call(int32_t deviceId, aclrtDeviceStatus *deviceStatus) override;
};

class HijackedFuncOfAclrtGetDeviceCountImpl : public decltype(AscendclImpHijackedType(&aclrtGetDeviceCountImpl)) {
public:
    explicit HijackedFuncOfAclrtGetDeviceCountImpl();
    aclError Call(uint32_t *count) override;
};

class HijackedFuncOfAclrtGetDeviceInfoImpl : public decltype(AscendclImpHijackedType(&aclrtGetDeviceInfoImpl)) {
public:
    explicit HijackedFuncOfAclrtGetDeviceInfoImpl();
    aclError Call(uint32_t deviceId, aclrtDevAttr attr, int64_t *value) override;
};

class HijackedFuncOfAclrtGetSocNameImpl : public decltype(HijackedFuncHelper(&aclrtGetSocNameImpl)) {
public:
    explicit HijackedFuncOfAclrtGetSocNameImpl();
    const char* Call() override;
    const char* EmptyFunc() override
    {
        return nullptr;
    }
};

class HijackedFuncOfAclrtLaunchKernelImpl : public decltype(AscendclImpHijackedType(&aclrtLaunchKernelImpl)) {
public:
    explicit HijackedFuncOfAclrtLaunchKernelImpl();
    aclError Call(aclrtFuncHandle funcHandle, uint32_t blockDim, const void *argsData,
                  size_t argsSize, aclrtStream stream) override;
    void Pre(aclrtFuncHandle funcHandle, uint32_t blockDim, const void *argsData,
             size_t argsSize, aclrtStream stream) override;
    aclError Post(aclError ret) override;
private:
    bool InitParam(aclrtFuncHandle funcHandle, uint32_t blockDim,
                   const void *argsData, size_t argsSize, aclrtStream stream);
    void ProfPre(const std::function<bool(void)> &func, const std::function<void(const std::string &)> &bbCountTask,
                 aclrtStream stm);
    void DoOperandRecord();
    void ProfPost();
    void ProfPreForInstrProf(const std::function<bool(void)> &func,const std::function<void(const std::string &)> &bbCountTask, rtStream_t stream);
 	bool PrepareDbiTaskForInstrProf(ProfDBIType mode, uint64_t memSize);
    void SanitizerPre();
    void SanitizerPost();
private:
    aclrtFuncHandle funcHandle_ {nullptr};
    uint32_t blockDim_ {0};
    aclrtStream stream_ {nullptr};
    void *argsData_ {nullptr};
    size_t argsSize_ {0};
    std::shared_ptr<ProfDataCollect> profObj_ {nullptr};
    std::function<void()> refreshParamFunc_ {nullptr};
    int32_t devId_{0};
    uint8_t *memInfo_{nullptr};
    uint64_t memSize_{0};
    FuncContextSP funcCtx_{nullptr};
    LaunchContextSP launchCtx_ {nullptr};
    ArgsContextSP newArgsCtx_{nullptr};
    ArgsContextSP argsCtx_{nullptr};
    uint64_t regId_{0};
    bool skipSanitizer_{false}; // 是否跳过检测
    bool isSink_{false};
};

class HijackedFuncOfAclrtLaunchKernelV2Impl : public decltype(AscendclImpHijackedType(&aclrtLaunchKernelV2Impl)) {
public:
    explicit HijackedFuncOfAclrtLaunchKernelV2Impl();
    aclError Call(aclrtFuncHandle funcHandle, uint32_t numBlocks, const void *argsData,
                  size_t argsSize, aclrtLaunchKernelCfg *cfg, aclrtStream stream) override;
    void Pre(aclrtFuncHandle funcHandle, uint32_t numBlocks, const void *argsData,
             size_t argsSize, aclrtLaunchKernelCfg *cfg, aclrtStream stream) override;
    aclError Post(aclError ret) override;
private:
    bool InitParam(aclrtFuncHandle funcHandle, uint32_t numBlocks,
                   const void *argsData, size_t argsSize, aclrtLaunchKernelCfg *cfg, aclrtStream stream);
    void ProfPre(const std::function<bool(void)> &func, const std::function<void(const std::string &)> &bbCountTask,
                 aclrtStream stm);
    void DoOperandRecord();
    void ProfPost();
    void ProfPreForInstrProf(const std::function<bool(void)> &func,const std::function<void(const std::string &)> &bbCountTask, rtStream_t stream);
 	bool PrepareDbiTaskForInstrProf(ProfDBIType mode, uint64_t memSize);
    void SanitizerPre();
    void SanitizerPost();
private:
    aclrtFuncHandle funcHandle_ {nullptr};
    uint32_t numBlocks_ {0};
    aclrtStream stream_ {nullptr};
    void *argsData_ {nullptr};
    size_t argsSize_ {0};
    aclrtLaunchKernelCfg *cfg_{nullptr};
    std::shared_ptr<ProfDataCollect> profObj_ {nullptr};
    std::function<void()> refreshParamFunc_ {nullptr};
    int32_t devId_{0};
    uint8_t *memInfo_{nullptr};
    uint64_t memSize_{0};
    FuncContextSP funcCtx_{nullptr};
    LaunchContextSP launchCtx_ {nullptr};
    ArgsContextSP newArgsCtx_{nullptr};
    ArgsContextSP argsCtx_{nullptr};
    uint64_t regId_{0};
    bool skipSanitizer_{false}; // 是否跳过检测
    bool isSink_{false};
};

class HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl : public decltype(AscendclImpHijackedType(&aclrtLaunchKernelWithHostArgsImpl)) {
public:
    explicit HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl();
    aclError Call(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream, aclrtLaunchKernelCfg *cfg,
                  void *hostArgs, size_t argsSize, aclrtPlaceHolderInfo *placeHolderArray, size_t placeHolderNum) override;
    void Pre(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream, aclrtLaunchKernelCfg *cfg,
             void *hostArgs, size_t argsSize, aclrtPlaceHolderInfo *placeHolderArray, size_t placeHolderNum) override;
    aclError Post(aclError ret) override;
private:
    bool InitParam(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream, aclrtLaunchKernelCfg *cfg,
                   void *hostArgs, size_t argsSize, aclrtPlaceHolderInfo *placeHolderArray, size_t placeHolderNum);
    void ProfPre(const std::function<bool(void)> &func, const std::function<void(const std::string &)> &bbCountTask,
                 aclrtStream stm);
    void ProfPreForInstrProf(const std::function<bool(void)> &func,const std::function<void(const std::string &)> &bbCountTask, rtStream_t stream);
 	bool PrepareDbiTaskForInstrProf(ProfDBIType mode, uint64_t memSize);
    void DoOperandRecord();
    void ProfPost();
    void SanitizerPre();
    void SanitizerPost();
private:
    aclrtFuncHandle funcHandle_ {nullptr};
    uint32_t blockDim_ {0};
    aclrtStream stream_ {nullptr};
    void *hostArgs_ {nullptr};
    size_t argsSize_ {0};
    aclrtLaunchKernelCfg *cfg_{nullptr};
    std::vector<aclrtPlaceHolderInfo> placeHolderArray_;
    size_t placeHolderNum_{0};
    std::shared_ptr<ProfDataCollect> profObj_ {nullptr};
    std::function<void()> refreshParamFunc_ {nullptr};
    int32_t devId_{0};
    uint8_t *memInfo_{nullptr};
    uint64_t memSize_{0};
    FuncContextSP funcCtx_{nullptr};
    LaunchContextSP launchCtx_ {nullptr};
    ArgsContextSP newArgsCtx_{nullptr};
    ArgsContextSP argsCtx_{nullptr};
    uint64_t regId_{0};
    bool skipSanitizer_{false}; // 是否跳过检测
    bool isSink_{false};
    KernelType kernelType_{KernelType::INVALID}; // 当前funcHandle对应的kernelType
};


class HijackedFuncOfAclrtLaunchKernelWithConfigImpl: public decltype(
    AscendclImpHijackedType(&aclrtLaunchKernelWithConfigImpl)) {
public:
    explicit HijackedFuncOfAclrtLaunchKernelWithConfigImpl();

    void Pre(aclrtFuncHandle funcHandle, uint32_t blockDim,
             aclrtStream stream, aclrtLaunchKernelCfg *cfg,
             aclrtArgsHandle argsHandle, void *reserve) override;

    aclError Call(aclrtFuncHandle funcHandle, uint32_t blockDim,
                  aclrtStream stream, aclrtLaunchKernelCfg *cfg,
                  aclrtArgsHandle argsHandle, void *reserve) override;

    aclError Post(aclError ret) override;

private:
    bool InitParam(
        aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream,
        aclrtLaunchKernelCfg *cfg, aclrtArgsHandle argsHandle, void *reserve);

    void ProfPost();
    void ProfPre(const std::function<bool(void)> &func,
                 const std::function<void(const std::string &)> &bbCountTask, aclrtStream stm);
    void DoOperandRecord();
    void ProfPreForInstrProf(const std::function<bool(void)> &func,const std::function<void(const std::string &)> &bbCountTask, rtStream_t stream);
 	bool PrepareDbiTaskForInstrProf(ProfDBIType mode, uint64_t memSize);

    void SanitizerPre();
    void SanitizerPost();

private:
    aclrtFuncHandle funcHandle_ {nullptr};
    uint32_t blockDim_ {0};
    aclrtStream stream_ {nullptr};
    aclrtLaunchKernelCfg *cfg_ {nullptr};
    aclrtArgsHandle argsHandle_ {nullptr};;
    void *reserve_ {nullptr};
    std::function<void()> refreshParamFunc_ {nullptr};
    LaunchContextSP launchCtx_ {nullptr};
    uint8_t *memInfo_{nullptr};
    uint64_t memSize_{0};
    std::shared_ptr<ProfDataCollect> profObj_ {nullptr};
    bool skipSanitizer_{false}; // 是否跳过检测
    bool isSink_{false};
    uint64_t regId_{0};
    FuncContextSP funcCtx_{nullptr};
    ArgsContextSP newArgsCtx_{nullptr};
    int32_t devId_{0};

};

class HijackedFuncOfAclrtKernelArgsInitByUserMemImpl : public decltype(AscendclImpHijackedType(&aclrtKernelArgsInitByUserMemImpl)) {
public:
    explicit HijackedFuncOfAclrtKernelArgsInitByUserMemImpl();
    void Pre(aclrtFuncHandle funcHandle, aclrtArgsHandle argsHandle, void *userHostMem, size_t actualArgsSize) override;
    aclError Post(aclError ret) override;
private:
    aclrtFuncHandle funcHandle_{nullptr};
    aclrtArgsHandle argsHandle_{nullptr};
};

class HijackedFuncOfAclmdlRICaptureBeginImpl: public decltype(AscendclImpHijackedType(&aclmdlRICaptureBeginImpl)) {
public:
    explicit HijackedFuncOfAclmdlRICaptureBeginImpl();
    void Pre(aclrtStream stream, aclmdlRICaptureMode mode) override;
};

class HijackedFuncOfAclmdlRICaptureEndImpl: public decltype(AscendclImpHijackedType(&aclmdlRICaptureEndImpl)) {
public:
    explicit HijackedFuncOfAclmdlRICaptureEndImpl();
    void Pre(aclrtStream stream, aclmdlRI *modeRI) override;
};

class HijackedFuncOfAclmdlRIBindStreamImpl
    : public decltype(AscendclImpHijackedType(&aclmdlRIBindStreamImpl)) {
public:
    explicit HijackedFuncOfAclmdlRIBindStreamImpl();
    void Pre(aclmdlRI modelRI, aclrtStream stream, uint32_t flag) override;
};

class HijackedFuncOfAclmdlRIUnbindStreamImpl
    : public decltype(AscendclImpHijackedType(&aclmdlRIUnbindStreamImpl)) {
public:
    explicit HijackedFuncOfAclmdlRIUnbindStreamImpl();
    void Pre(aclmdlRI modelRI, aclrtStream stream) override;
};
