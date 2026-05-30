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

// 该文件定义劫持函数对象
// 这个文件中尽量不要引入各种私有定义，保持依赖的干净

#ifndef __RUNTIME_HIJACKED_FUNC_H__
#define __RUNTIME_HIJACKED_FUNC_H__

#include <vector>
#include <memory>
#include "runtime.h"
#include "core/HijackedFuncTemplate.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "runtime/inject_helpers/ProfConfig.h"

template <>
struct EmptyFuncError<rtError_t> {
    // `RT_ERROR_RESERVED' 用于代表 rtError_t 类型中原始函数获取失败
    static constexpr rtError_t VALUE = RT_ERROR_RESERVED;
};

// For rtDevBinaryRegister
class HijackedFuncOfDevBinaryRegister : public decltype(HijackedFuncHelper(&rtDevBinaryRegister)) {
public:
    explicit HijackedFuncOfDevBinaryRegister();
    void Pre(const rtDevBinary_t *bin, void **hdl) override;
    rtError_t Post(rtError_t ret) override;
private:
    const rtDevBinary_t *bin_{nullptr};
    rtDevBinary_t *stubBin_{nullptr};
    void **hdl_{nullptr};
}; // class HijackedFuncOfDevBinaryRegister

// For rtRegisterAllKernel
class HijackedFuncOfRegisterAllKernel : public decltype(HijackedFuncHelper(&rtRegisterAllKernel)) {
public:
    explicit HijackedFuncOfRegisterAllKernel();
    void Pre(const rtDevBinary_t *bin, void **hdl) override;
    rtError_t Call(const rtDevBinary_t *bin, void **hdl) override;
    rtError_t Post(rtError_t ret) override;
private:
    const rtDevBinary_t *bin_{nullptr};
    void **hdl_{nullptr};
}; // class HijackedFuncOfRegisterAllKernel

// For rtFunctionRegister
class HijackedFuncOfFunctionRegister : public decltype(HijackedFuncHelper(&rtFunctionRegister)) {
public:
    explicit HijackedFuncOfFunctionRegister();
    void Pre(void *binHandle, const void *stubFunc, const char *stubName,
        const void *kernelInfoExt, uint32_t funcMode) override;
    rtError_t Post(rtError_t ret) override;
private:
    void *binHandle_{nullptr};
    const void *stubFunc_{nullptr};
}; // class HijackedFuncOfFunctionRegister

// For rtKernelLaunch
class HijackedFuncOfKernelLaunch : public decltype(HijackedFuncHelper(&rtKernelLaunch)) {
public:
    explicit HijackedFuncOfKernelLaunch();
    void Pre(const void *stubFunc, uint32_t blockDim, void *args,
        uint32_t argsSize, rtSmDesc_t *smDesc, rtStream_t stm) override;
    rtError_t Call(const void *stubFunc, uint32_t blockDim, void *args,
        uint32_t argsSize, rtSmDesc_t *smDesc, rtStream_t stm) override;
    rtError_t Post(rtError_t ret) override;
private:
    void InitParam(const void *stubFunc, uint32_t blockDim, void *args,
                   uint32_t argsSize, rtSmDesc_t *smDesc, rtStream_t stm);
    void ProfPre(const std::function<bool(void)> &func,
                 const std::function<void(const std::string &)> &bbCountTask, rtStream_t stm);
    void SanitizerPre();
    bool PrepareDbiTask(ProfDBIType mode, uint64_t memSize);
    void ProfPost();
    void SanitizerPost() const;
    void ProfPreForInstrProf(const std::function<bool(void)> &func, const std::function<void(const std::string &)> &bbCountTask, rtStream_t stm);
private:
    const void *stubFunc_{nullptr};
    std::string bbStubFuncStr_;
    rtDevBinary_t *stubBinPtr_{nullptr};
    void *stubHdl_{nullptr};
    uint32_t blockDim_{0};
    void *args_{nullptr};
    uint32_t argsSize_{0};
    rtSmDesc_t *smDesc_{nullptr};
    uint8_t *memInfo_{nullptr}; // 额外引入, 需要在Pre清空
    uint64_t memSize_{0}; // 额外引入, 需要在Pre清空
    std::vector<uint8_t> argsVec_; // 额外引入, 需要在Pre清空
    rtStream_t stm_{};
    std::shared_ptr<ProfDataCollect> profObj_; // 额外引入, 需要在Pre清空
    bool skipSanitizer_{false}; // 是否跳过检测
    uint64_t launchId_{0};
    uint64_t regId_{0};
    std::function<void()> refreshParamFunc_;
    int32_t devId_{0};
    bool isSink_{false};
    std::vector<Elf64_Shdr> sections_;
}; // class HijackedFuncOfKernelLaunch

// For rtKernelLaunchWithHandleV2
class HijackedFuncOfKernelLaunchWithHandleV2 : public decltype(HijackedFuncHelper(&rtKernelLaunchWithHandleV2)) {
public:
    explicit HijackedFuncOfKernelLaunchWithHandleV2();
    void Pre(void *hdl, const uint64_t tilingKey, uint32_t blockDim, rtArgsEx_t *argsInfo,
        rtSmDesc_t *smDesc, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo) override;
    rtError_t Call(void *hdl, const uint64_t tilingKey, uint32_t blockDim,
        rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm,
        const rtTaskCfgInfo_t *cfgInfo) override;
    rtError_t Post(rtError_t ret) override;

private:
    void InitParam(void *hdl, const uint64_t tilingKey, uint32_t blockDim, rtArgsEx_t *argsInfo,
        rtSmDesc_t *smDesc, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo);

    void ProfPre(const std::function<bool(void)> &func,
                 const std::function<void(const std::string &)> &bbCountTask, rtStream_t stm);
    void ProfPreForInstrProf(const std::function<bool(void)> &func,
                             const std::function<void(const std::string &)> &bbCountTask, rtStream_t stm);
    bool PrepareDbiTask(ProfDBIType mode, uint64_t memSize);
    void SanitizerPre();

    void ProfPost();
    void SanitizerPost();
private:
    void *hdl_{nullptr};
    void *stubHdl_{nullptr};
    rtDevBinary_t *stubBinPtr_{nullptr};
    rtArgsEx_t *argsInfo_{nullptr};
    uint8_t *memInfo_{nullptr}; // 额外引入, 需要在Pre清空
    uint64_t memSize_{0}; // 额外引入, 需要在Pre清空
    uint32_t blockDim_{0};
    std::vector<uint8_t> argsVec_; // 额外引入, 需要在Pre清空
    std::vector<rtHostInputInfo_t> hostInput_; // 额外引入, 需要在Pre清空
    rtArgsEx_t newArgsInfo_{}; // 额外引入, 需要在Pre清空
    rtStream_t stm_{};
    std::shared_ptr<ProfDataCollect> profObj_; // 额外引入, 需要在Pre清空
    bool skipSanitizer_{false}; // 是否跳过检测
    uint64_t launchId_{0};
    uint64_t regId_{0};
    rtSmDesc_t *smDesc_{nullptr};
    const rtTaskCfgInfo_t *cfgInfo_{nullptr};
    std::function<void()> refreshParamFunc_;
    uint64_t tilingKey_{0};
    int32_t devId_{0};
    bool isSink_{false};
    std::vector<Elf64_Shdr> sections_;
}; // class HijackedFuncOfKernelLaunchWithHandleV2

class HijackedFuncOfAiCpuKernelLaunchExWithArgs : public decltype(HijackedFuncHelper(&rtAicpuKernelLaunchExWithArgs)) {
public:
    explicit HijackedFuncOfAiCpuKernelLaunchExWithArgs();
    void Pre(const uint32_t kernelType, const char *const opName, const uint32_t blockDim,
        const rtAicpuArgsEx_t *argsInfo, rtSmDesc_t *const smDesc, const rtStream_t stm, const uint32_t flags) override;
    rtError_t Post(rtError_t ret) override;
}; // class HijackedFuncOfAiCpuKernelLaunchExWithArgs

// For rtKernelLaunchWithFlagV2
class HijackedFuncOfKernelLaunchWithFlagV2 : public decltype(HijackedFuncHelper(&rtKernelLaunchWithFlagV2)) {
public:
    explicit HijackedFuncOfKernelLaunchWithFlagV2();
    void Pre(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
        rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags, const rtTaskCfgInfo_t *cfgInfo) override;
    rtError_t Post(rtError_t ret) override;
    rtError_t Call(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                   rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags, const rtTaskCfgInfo_t *cfgInfo) override;
    rtError_t EmptyFunc() override {return RT_ERROR_RESERVED;}

private:
    void InitParam(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                   rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags, const rtTaskCfgInfo_t *cfgInfo);
    void ProfPre(const std::function<bool(void)> &func,
                 const std::function<void(const std::string &)> &bbCountTask, rtStream_t stm);
    void ProfPreForInstrProf(const std::function<bool(void)> &func, const std::function<void(const std::string &)> &bbCountTask, rtStream_t stm);
    bool PrepareDbiTask(ProfDBIType mode, uint64_t memSize);
    void SanitizerPre();

    void ProfPost();
    void SanitizerPost();

private:
    const void *stubFunc_{nullptr};
    std::string bbStubFuncStr_;
    rtDevBinary_t *stubBinPtr_{nullptr};
    void *stubHdl_{nullptr};
    uint32_t blockDim_{0};
    rtArgsEx_t *argsInfo_{nullptr};
    uint8_t *memInfo_{nullptr}; // 额外引入, 需要在Pre清空
    uint64_t memSize_{0}; // 额外引入, 需要在Pre清空
    std::vector<uint8_t> argsVec_; // 额外引入, 需要在Pre清空
    std::vector<rtHostInputInfo_t> hostInput_; // 额外引入, 需要在Pre清空
    rtArgsEx_t newArgsInfo_{}; // 额外引入, 需要在Pre清空
    rtStream_t stm_{};
    std::shared_ptr<ProfDataCollect> profObj_; // 额外引入, 需要在Pre清空
    bool skipSanitizer_{false}; // 是否跳过检测
    uint64_t launchId_{0};
    uint64_t regId_{0};
    uint32_t flags_{0};
    rtSmDesc_t *smDesc_{nullptr};
    const rtTaskCfgInfo_t *cfgInfo_{nullptr};
    std::function<void()> refreshParamFunc_;
    bool isSink_{false};
    int32_t devId_{0};
    std::vector<Elf64_Shdr> sections_;
}; // class HijackedFuncOfKernelLaunchWithFlagV2

// For rtDevBinaryUnRegister
class HijackedFuncOfDevBinaryUnRegister : public decltype(HijackedFuncHelper(&rtDevBinaryUnRegister)) {
public:
    explicit HijackedFuncOfDevBinaryUnRegister();
    void Pre(void *hdl) override;
private:
    void *hdl_{nullptr};
}; // class HijackedFuncOfDevBinaryUnRegister

class HijackedFuncOfSetExceptionExtInfo : public decltype(HijackedFuncHelper(&rtSetExceptionExtInfo)) {
public:
    explicit HijackedFuncOfSetExceptionExtInfo();
    void Pre(const rtArgsSizeInfo_t * const sizeInfo) override;
    rtError_t Post(rtError_t ret) override;
private:
    const rtDevBinary_t *bin_{nullptr};
    void **hdl_{nullptr};
    rtDevBinary_t stubBin_{}; // 额外引入, 需要在Pre清空
};

class HijackedFuncOfGetDevice : public decltype(HijackedFuncHelper(&rtGetDevice)) {
public:
    explicit HijackedFuncOfGetDevice();
    void Pre(int32_t *devId) override;
    rtError_t Call(int32_t *devId) override;
private:
    int32_t devId_{0};
};

class HijackedFuncOfSetDevice : public decltype(HijackedFuncHelper(&rtSetDevice)) {
public:
    explicit HijackedFuncOfSetDevice();
    ~HijackedFuncOfSetDevice() override = default;
    void Pre(int32_t devId) override;
    rtError_t Call(int32_t devId) override;
    rtError_t Post(rtError_t ret) override;
private:
    int32_t devId_{0};
    char socVersion_[64];
};

class HijackedFuncOfSetDeviceV2 : public decltype(HijackedFuncHelper(&rtSetDeviceV2)) {
public:
    explicit HijackedFuncOfSetDeviceV2();
    void Pre(int32_t devId, rtDeviceMode deviceMode) override;
    rtError_t Call(int32_t devId, rtDeviceMode deviceMode) override;
private:
    int32_t devId_{0};
};

class HijackedFuncOfDeviceReset : public decltype(HijackedFuncHelper(&rtDeviceReset)) {
public:
    explicit HijackedFuncOfDeviceReset();
    void Pre(int32_t devId) override;
    rtError_t Call(int32_t devId) override;
    rtError_t Post(rtError_t ret) override;

private:
    int32_t devId_{0};
};

class HijackedFuncOfDeviceResetEx : public decltype(HijackedFuncHelper(&rtDeviceResetEx)) {
public:
    explicit HijackedFuncOfDeviceResetEx();
    void Pre(int32_t devId) override;
    rtError_t Call(int32_t devId) override;
    rtError_t Post(rtError_t ret) override;

private:
    int32_t devId_{0};
};

class HijackedFuncOfSetDeviceEx : public decltype(HijackedFuncHelper(&rtSetDeviceEx)) {
public:
    explicit HijackedFuncOfSetDeviceEx();
    void Pre(int32_t devId) override;
    rtError_t Call(int32_t devId) override;
private:
    int32_t devId_{0};
};

class HijackedFuncOfCtxCreate : public decltype(HijackedFuncHelper(&rtCtxCreate)) {
public:
    explicit HijackedFuncOfCtxCreate();
    void Pre(void **createCtx, uint32_t flags, int32_t devId) override;
    rtError_t Call(void **createCtx, uint32_t flags, int32_t devId) override;
private:
    int32_t devId_{0};
};

class HijackedFuncOfCtxCreateV2 : public decltype(HijackedFuncHelper(&rtCtxCreateV2)) {
public:
    explicit HijackedFuncOfCtxCreateV2();
    void Pre(void **createCtx, uint32_t flags, int32_t devId, rtDeviceMode deviceMode) override;
    rtError_t Call(void **createCtx, uint32_t flags, int32_t devId, rtDeviceMode deviceMode) override;
private:
    int32_t devId_{0};
};

class HijackedFuncOfCtxCreateEx : public decltype(HijackedFuncHelper(&rtCtxCreateEx)) {
public:
    explicit HijackedFuncOfCtxCreateEx();
    void Pre(void **ctx, uint32_t flags, int32_t devId) override;
    rtError_t Call(void **ctx, uint32_t flags, int32_t devId) override;
private:
    int32_t devId_{0};
};

class HijackedFuncOfDeviceStatusQuery : public decltype(HijackedFuncHelper(&rtDeviceStatusQuery)) {
public:
    explicit HijackedFuncOfDeviceStatusQuery();
    void Pre(const uint32_t devId, rtDeviceStatus *deviceStatus) override;
    rtError_t Call(const uint32_t devId, rtDeviceStatus *deviceStatus) override;

private:
    int32_t devId_{0};
};

class HijackedFuncOfGetSocVersion : public decltype(HijackedFuncHelper(&rtGetSocVersion)) {
public:
    explicit HijackedFuncOfGetSocVersion();
    rtError_t Call(char *version, const uint32_t maxLen) override;
};

class HijackedFuncOfGetAiCoreCount : public decltype(HijackedFuncHelper(&rtGetAiCoreCount)) {
public:
    explicit HijackedFuncOfGetAiCoreCount();
    rtError_t Call(uint32_t *aiCoreCnt) override;
};


class HijackedFuncOfMalloc : public decltype(HijackedFuncHelper(&rtMalloc)) {
public:
    explicit HijackedFuncOfMalloc();
    ~HijackedFuncOfMalloc() override = default;
    void Pre(void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId) override;
    rtError_t Call(void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId) override;
    rtError_t Post(rtError_t ret) override;
private:
    void **devPtr_{nullptr};
    uint64_t size_{0};
    rtMemType_t type_{};
    size_t actualSize_{0};
};

class HijackedFuncOfFree : public decltype(HijackedFuncHelper(&rtFree)) {
public:
    explicit HijackedFuncOfFree();
    ~HijackedFuncOfFree() override = default;
    void Pre(void *devPtr) override;
    rtError_t Call(void *devPtr) override;

private:
    void *devPtr_{nullptr};
    void *actualPtr_{nullptr};
};

class HijackedFuncOfMapMem : public decltype(HijackedFuncHelper(&rtMapMem)) {
public:
    explicit HijackedFuncOfMapMem();
    ~HijackedFuncOfMapMem() override = default;
    void Pre(void* devPtr, size_t size, size_t offset, rtDrvMemHandle_t* handle, uint64_t flags) override;
    rtError_t Post(rtError_t ret) override;
private:
    void *devPtr_{nullptr};
    uint64_t size_{0};
};

class HijackedFuncOfUnmapMem : public decltype(HijackedFuncHelper(&rtUnmapMem)) {
public:
    explicit HijackedFuncOfUnmapMem();
    ~HijackedFuncOfUnmapMem() override = default;
    void Pre(void* devPtr) override;
};

class HijackedFuncOfMemset : public decltype(HijackedFuncHelper(&rtMemset)) {
public:
    explicit HijackedFuncOfMemset();
    ~HijackedFuncOfMemset() override = default;
    void Pre(void *devPtr, uint64_t destMax, uint32_t val, uint64_t cnt) override;
};

class HijackedFuncOfMemcpy : public decltype(HijackedFuncHelper(&rtMemcpy)) {
public:
    explicit HijackedFuncOfMemcpy();
    ~HijackedFuncOfMemcpy() override = default;
    void Pre(void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind) override;
};

class HijackedFuncOfGetDeviceCount : public decltype(HijackedFuncHelper(&rtGetDeviceCount)) {
public:
    explicit HijackedFuncOfGetDeviceCount();
    ~HijackedFuncOfGetDeviceCount() override = default;
    rtError_t Call(int32_t *cnt) override;
};

class HijackedFuncOfStreamSynchronizeWithTimeout : public decltype(HijackedFuncHelper(&rtStreamSynchronizeWithTimeout)) {
public:
    explicit HijackedFuncOfStreamSynchronizeWithTimeout();
    ~HijackedFuncOfStreamSynchronizeWithTimeout() override = default;
    rtError_t Call(rtStream_t stream, int32_t timeout) override;
};

class HijackedFuncOfGetDeviceInfo : public decltype(HijackedFuncHelper(&rtGetDeviceInfo)) {
public:
    explicit HijackedFuncOfGetDeviceInfo();
    ~HijackedFuncOfGetDeviceInfo() override = default;
    rtError_t Call(uint32_t deviceId, int32_t moduleType, int32_t infoType, int64_t *val) override;
};

class HijackedFuncOfIpcSetMemoryName : public decltype(HijackedFuncHelper(&rtIpcSetMemoryName)) {
public:
    explicit HijackedFuncOfIpcSetMemoryName();
    ~HijackedFuncOfIpcSetMemoryName() override = default;
    rtError_t Call(const void *ptr, uint64_t byteCount, char *name, uint32_t len) override;
};

class HijackedFuncOfIpcDestroyMemoryName: public decltype(HijackedFuncHelper(&rtIpcDestroyMemoryName)) {
public:
    explicit HijackedFuncOfIpcDestroyMemoryName();
    ~HijackedFuncOfIpcDestroyMemoryName() override = default;
    rtError_t Call(const char *name) override;
};

class HijackedFuncOfIpcOpenMemory: public decltype(HijackedFuncHelper(&rtIpcOpenMemory)) {
public:
    explicit HijackedFuncOfIpcOpenMemory();
    ~HijackedFuncOfIpcOpenMemory() override = default;
    rtError_t Call(void **ptr, const char *name) override;
};

class HijackedFuncOfIpcCloseMemory: public decltype(HijackedFuncHelper(&rtIpcCloseMemory)) {
public:
    explicit HijackedFuncOfIpcCloseMemory();
    ~HijackedFuncOfIpcCloseMemory() override = default;
    rtError_t Call(const void *ptr) override;
};

class HijackedFuncOfModelBindStream: public decltype(HijackedFuncHelper(&rtModelBindStream)) {
public:
    explicit HijackedFuncOfModelBindStream();

    void Pre(rtModel_t mdl, rtStream_t stm, uint32_t flag) override;

    ~HijackedFuncOfModelBindStream() override = default;
};

class HijackedFuncOfGetL2CacheOffset : public decltype(HijackedFuncHelper(&rtGetL2CacheOffset)) {
public:
    explicit HijackedFuncOfGetL2CacheOffset();
    ~HijackedFuncOfGetL2CacheOffset() override = default;
    rtError_t Call(uint32_t deviceId, uint64_t *offset) override;
};

class HijackedFuncOfCtxGetOverflowAddr : public decltype(HijackedFuncHelper(&rtCtxGetOverflowAddr)) {
public:
    explicit HijackedFuncOfCtxGetOverflowAddr();
    void Pre(void **overflowAddr) override;
    rtError_t Post(rtError_t ret) override;

private:
    void **overflowAddr_;
};

#endif //__RUNTIME_HIJACKED_FUNC_H__
