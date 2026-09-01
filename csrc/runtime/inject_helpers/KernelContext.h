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

#ifndef __KERNEL_CONTEXT_H__
#define __KERNEL_CONTEXT_H__

#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include <thread>
#include <mutex>

#include "nlohmann/json.hpp"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "runtime/RuntimeOrigin.h"
#include "runtime.h"
#include "include/thirdparty/prof.h"
#include "ascend_dump.h"
#include "hccl.h"
#include "runtime/inject_helpers/ArgsContext.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "utils/TypeTraits.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"
#include "ProfTask.h"
#include "LaunchManager.h"
#include "KernelMetaDataParser.h"

using KernelHandle = void;
using StubFunc = void;
using KernelArgsType = void;

/*
 * kernel infomation set, which contains:
 * 1. kernel input args infomation, include input/output/workspace/tiling and so on
 * 2. kernel names and kernel register id of each kernel object
 * 3. kernel launch and kernel register id counter (can be used by kernel replacement)
 * 4. kernel context save:
 *  this current only contain kernel object, kernel args, kernel register id, kernel launch id.
 *
 * limit:
 * 1. only contains infomation of original kernel
 * 2. the saved data, is used by msopt, be cautious to change `ContextConfig`
 *
 * future:
 * implement kernel launch id infomation
 *
 */
// include A2/A3/A5
const std::map<std::string, uint32_t> CORE_NUM = {
    {"Ascend910B1",       24},
    {"Ascend910B2",       24},
    {"Ascend910B3",       20},
    {"Ascend910B4",       20},
    {"Ascend910B2C",      24},
    {"Ascend910B4-1",     20},
    {"Ascend910_9391",    24},
    {"Ascend910_9392",    24},
    {"Ascend910_9381",    24},
    {"Ascend910_9382",    24},
    {"Ascend910_9372",    20},
    {"Ascend910_9362",    20},
    {"Ascend910_9363",    20},
    {"Ascend950DT_950x",  8},
    {"Ascend950DT_950y",  8},
    {"Ascend950PR_950z",  8},
    {"Ascend950DT_9571",  28},
    {"Ascend950DT_9572",  28},
    {"Ascend950DT_9573",  28},
    {"Ascend950DT_9574",  28},
    {"Ascend950DT_9575",  28},
    {"Ascend950DT_9576",  28},
    {"Ascend950DT_9577",  28},
    {"Ascend950DT_9578",  28},
    {"Ascend950PR_9579",  28},
    {"Ascend950PR_957b",  28},
    {"Ascend950PR_957c",  28},
    {"Ascend950PR_957d",  28},
    {"Ascend950DT_9581",  32},
    {"Ascend950DT_9582",  32},
    {"Ascend950DT_9583",  32},
    {"Ascend950DT_9584",  32},
    {"Ascend950DT_9585",  32},
    {"Ascend950DT_9586",  32},
    {"Ascend950DT_9587",  32},
    {"Ascend950DT_9588",  32},
    {"Ascend950PR_9589",  32},
    {"Ascend950PR_958b",  32},
    {"Ascend950DT_9591",  36},
    {"Ascend950DT_9592",  36},
    {"Ascend950DT_9595",  36},
    {"Ascend950DT_9596",  36},
    {"Ascend950PR_9599",  36},
    {"Ascend950DT_95A1",  36},
    {"Ascend950DT_95A2",  36},
};

const std::map<std::string, uint32_t> CORE_NUM_OF_310P = {
    {"Ascend310P1", 10},
    {"Ascend310P2", 10},
    {"Ascend310P3", 8},
    {"Ascend310P4", 10},
    {"Ascend310P5", 8},
    {"Ascend310P7", 8},
};
constexpr char const  *KERNEL_CONFIG_NAME = "kernel_config.bin";
constexpr char const  *KERNEL_BIN_NAME = "aicore_binary.o";
/*
 * For MC2
 */
struct AicpuLaunchArgs {
    uint32_t kernelType;
    std::string opName; // 这里使用const char *会造成内存踩踏，rtaicpukernellaunchwithargs使用的是char*，因此需要一步转换
    uint32_t blockDim;
    const rtAicpuArgsEx_t *argsInfo;
    rtSmDesc_t *smDesc;
    rtStream_t stm;
    uint32_t flags;
    bool isValid;
};
class KernelContext {
public:
    // 兼容旧接口
    using OpMemInfo = ::OpMemInfo;
    using AddrInfo = ::AddrInfo;
    using ContextConfig = ::DumperContext;
    using SecondPtrInfo = ::SecondPtrInfo;
    // 定义 handle 和 stubFunc 指针类型用于重载
    using KernelHandlePtr = TaggedType<KernelHandle const *, class KernelHandleTag>;
    using StubFuncPtr = TaggedType<StubFunc const *, class StubFuncTag>;
    // each kernel object has one or more names
    // used in file path of saving kernel object */
    // future: may report with kernel launch id */
    struct RegisterEvent {
        const KernelHandle *hdl;
        rtDevBinary_t bin; // kernel object binary data
        const rtDevBinary_t *stubBin;
        std::vector<std::string> kernelNames;
        std::vector<uint64_t> kernelOffsets;
        std::vector<uint8_t> binData;
        bool isKernelWithDBI = false;
        bool hasDebugLine = false;
    };

    struct LaunchEvent {
        bool launchWithHandle;
        // rtKernelLaunch
        const StubFunc *stubFunc;
        // rtKernelLaunchWithHandleV2
        const KernelHandle *hdl;
        bool hasTilingKey;
        uint64_t tilingKey;
        uint64_t pcStartAddr;
        const rtArgsEx_t *argsInfo;

        uint32_t blockDim;
        std::string kernelName;
        rtArgsEx_t argsInfoCopy;
        bool isSink;
    };

    struct StubFuncInfo {
        const KernelHandle *hdl;
        std::string kernelName;
        std::string kernelInfoExt;
        uint32_t funcMode;
    };

    struct StubFuncArgs {
        StubFunc const *originHandle;
        StubFunc const *dbiHandle;
    };

    struct KernelHandleArgs {
        KernelHandle const *originHandle;
        KernelHandle const *dbiHandle;
        uint64_t tilingKey;
    };

    class DeviceContext {
    public:
        explicit DeviceContext(KernelContext &kernelContext);
        ~DeviceContext();

        void AddLaunchEvent(const StubFunc *stubFunc, StubFuncInfo const &info, uint32_t blockDim,
            const rtArgsEx_t *argsInfo, rtStream_t stm);
        void AddLaunchEvent(const KernelHandle *hdl, uint64_t tilingKey, uint32_t blockDim,
            const rtArgsEx_t *argsInfo, std::string const &kernelName, rtStream_t stm);
        OpMemInfo &GetOpMemInfo();
        std::vector<OpMemInfo> &GetOpMemInfoHistory() { return memInfoHistory_; }
        bool GetLastLaunchEvent(LaunchEvent &event) const;
        void SetArgsSize(const rtArgsSizeInfo_t * const sizeInfo);
        void ParseMetaDataFromBinary(rtDevBinary_t const &binary, const rtArgsEx_t *argsInfo = nullptr);
        bool Save(const std::string &outputDir, uint64_t launchId = UINT64_MAX);
        bool SaveAicore(const std::string &outputDir, uint64_t launchId = UINT64_MAX);
        uint64_t GetRegisterId(uint64_t launchId);
        std::string GetLaunchName(uint64_t launchId = UINT64_MAX);
        uint64_t GetLaunchId() const;
        uint32_t GetBlockId();
        bool GetLaunchEvent(uint64_t launchId, LaunchEvent &event) const;
        void SetMC2Flag(const bool &isMC2) { this->isMC2_ = isMC2; }
        bool GetMC2Flag() const { return this->isMC2_; }
        void SetHcclComm(const HcclComm &comm) { this->hcclComm_ = comm; }
        HcclComm GetHcclComm() { return this->hcclComm_; }
        void SetLcclFlag(const bool &isLccl) { this->isLccl_ = isLccl; }
        bool GetLcclFlag() const { return this->isLccl_; }
        void ArchiveMemInfo() { memInfoHistory_.push_back(memInfo_); };
        void ClearArgsInfo()
        {
            memInfo_.Clear();
        }
        template <typename ArgsT>
        bool GetPcStartAddr(ArgsT const &args, uint64_t &pcStartAddr) const;
        template <typename ArgsT>
        bool UpdatePcStartAddrByDbi(uint64_t launchId, ArgsT const &args);
        bool GetMc2CtxFlag() const { return this->memInfo_.hasMc2Ctx; }

        // 用于获取回调类型的kernel任务，然后执行下发的callback函数
        // 本质就是cq一直获取任务，然后执行任务
        void ListenCallbackTask(int32_t devId);
        bool SubscribeReport(rtStream_t stm);
        bool GetKernelAddr(StubFuncArgs const &args, uint64_t &kernelAddr) const;
        bool GetKernelAddr(KernelHandleArgs const &args, uint64_t &kernelAddr) const;
    private:
        // dump kernel args including input data, tiling data
        bool DumpKernelArgs(const std::string &outputDir, uint64_t launchId, ContextConfig &config);

        bool GetKernelOffset(StubFuncArgs const &args, uint64_t &kernelOffset) const;
        bool GetKernelOffset(KernelHandleArgs const &args, uint64_t &kernelOffset) const;

        KernelContext &kernelContext_;
        OpMemInfo memInfo_;
        std::vector<OpMemInfo> memInfoHistory_;
        std::vector<LaunchEvent> launchEvents_;
        std::mutex deviceMutex_;
        bool isMC2_ = false;
        bool isLccl_ = false;
        HcclComm hcclComm_ = nullptr;
        std::atomic_bool stopListen_{true};
        std::thread listenThread_;
        int32_t visibleDevId_{0};
    };

    friend class DeviceContext;

    static KernelContext &Instance()
    {
        static KernelContext inst;
        return inst;
    }

    // 根据 hdl 查询 pcStart 地址
    bool GetPcStartAddr(KernelHandlePtr hdl, uint64_t &pcStartAddr);

    uint64_t GetNextRegisterId() const { return registerEvents_.size(); }

    uint64_t GetLaunchId() const;

    // get latest launch event name
    // we can set default param launchId if we want to get more launch
    std::string GetLaunchName(uint64_t launchId = UINT64_MAX);

    uint32_t GetBlockId();

    bool GetRegisterEvent(uint64_t regId, RegisterEvent &event);

    uint64_t GetRegisterId(uint64_t launchId);

    uint64_t GetRegisterId(const void *key, bool withHandle);

    bool DumpKernelObject(const KernelHandle *hdl, const std::string &outputDir, const std::string &savedFileName);

    bool DumpKernelObject(uint64_t regId, const std::string &outputPath);

    // increase launch id, save launch id, kernel name, ...
    void AddLaunchEvent(const KernelHandle *hdl, uint64_t tilingKey, uint32_t blockDim,
        const rtArgsEx_t *argsInfo, rtStream_t stm);

    void AddLaunchEvent(const StubFunc *stubFunc, uint32_t blockDim, const rtArgsEx_t *argsInfo, rtStream_t stm);

    void AddFuncRegisterEvent(const KernelHandle *binHdl, const StubFunc *stubFunc, const char *stubName,
                              const void *kernelInfoExt, uint32_t funcMode);

    // increase register id, save register id , kernel bin, ...
    // current used by KernelBinManager
    void AddHdlRegisterEvent(const KernelHandle *hdl, const rtDevBinary_t *bin,
        const rtDevBinary_t *stubBin = nullptr);

    bool HasSimtSymbol() const { return hasSimt_; }

    void SetArgsSize(const rtArgsSizeInfo_t * const sizeInfo);

    void ParseMetaDataFromBinary(rtDevBinary_t const &binary, const rtArgsEx_t *argsInfo = nullptr);

    void ArchiveMemInfo();

    // save kernel object, kernel args, context config
    bool Save(const std::string &outputDir, uint64_t launchId = UINT64_MAX);

    bool SaveAicore(const std::string &outputDir, uint64_t launchId = UINT64_MAX); // msopt MC2 落盘aicore.o

    bool CheckHdlVaild(const KernelHandle* hdl) const
    {
        return hdlToRegId_.find(hdl) != hdlToRegId_.end();
    }

    bool CheckStubValid(const StubFunc* stub) const
    {
        return stubInfo_.find(stub) != stubInfo_.end();
    }

    // kernel name is get from binary when launched by rtKernelLaunchWithHandleV2
    std::string GetNameByTilingKey(const KernelHandle *handle, uint64_t tilingKey);

    /**
     * @brief 通过二进制注册的 handle 反查二进制
     * @param hdl 要查询的二进制注册句柄
     * @param bin 查询得到的二进制信息，以 rtDevBinary_t 类型承载，注意调用者使用时需要
     * 对 bin->data 二进制进行拷贝，防止内存被释放
     * @param getStubBin 是否获取动态插桩后的二进制，默认获取原始二进制
     * @return true  获取成功
     *         false 在已注册的信息中找不到对应 handle
     */
    bool GetDevBinary(KernelHandlePtr hdl, rtDevBinary_t &bin, bool getStubBin = false) const;

    /**
     * @brief 通过函数注册的 stubFunc 反查二进制
     * @param stubFunc 要查询的函数注册句柄
     * @param bin 查询得到的二进制信息，以 rtDevBinary_t 类型承载，注意调用者使用时需要
     * 对 bin->data 二进制进行拷贝，防止内存被释放
     * @param getStubBin 是否获取动态插桩后的二进制，默认获取原始二进制
     * @return true  获取成功
     *         false 在已注册的信息中找不到对应 stubFunc
     */
    bool GetDevBinary(StubFuncPtr stubFunc, rtDevBinary_t &bin, bool getStubBin = false) const;

    /**
     * @brief 获取最新的算子执行事件
     * @param [out] event 获取到的最新的算子执行事件，仅当返回值为 true 时有效
     * @return true  获取成功
     *         false 获取失败，当前未有注册的算子执行事件
     */
    bool GetLastLaunchEvent(LaunchEvent &event) const;

    /**
     * @brief 根据函数或二进制注册句柄上报算子二进制
     * @tparam 句柄类型（KernelHandlePtr 或 StubFuncPtr）
     * @param ptr 句柄指针
     */

    /**
     * @brief 根据 stubFunc 句柄查询 stubFunc 注册信息
     * @param stubFunc 要查询的句柄
     * @param stubFuncInfo 查询到的 stubFunc 注册信息，仅当返回值为 true 时有效
     */
    bool GetStubFuncInfo(StubFuncPtr stubFunc, StubFuncInfo &stubFuncInfo) const;

    /**
     * @brief 更新 registerEvent 中的 dbi 二进制信息
     */
    bool SetDbiBinary(uint64_t registerId, rtDevBinary_t const &devBinary);

    void ClearArgsInfo();

    template<typename Ptr>
    void ReportKernelBinary(Ptr ptr) const
    {
        rtDevBinary_t binary {};
        if (!GetDevBinary(ptr, binary)) {
            return;
        }
        PacketHead head { PacketType::KERNEL_BINARY };
        std::string buffer(static_cast<char const *>(binary.data), binary.length);
        LocalDevice::Local().Notify(Serialize(head, buffer.size()) + std::move(buffer));
    }

    OpMemInfo &GetOpMemInfo();
    std::vector<OpMemInfo> &GetOpMemInfoHistory() { return this->GetDeviceContext().GetOpMemInfoHistory(); }
    static AicpuLaunchArgs& GetAicpuLaunchArgs()
    {
        static thread_local AicpuLaunchArgs aicpuLaunchArgs{0, "", 0, nullptr, nullptr, nullptr, 0, false};
        return aicpuLaunchArgs;
    }

    bool GetLaunchEvent(uint64_t launchId, LaunchEvent &event) const;

    DeviceContext const &GetDeviceContext() const;
    DeviceContext &GetDeviceContext();

    void SetMC2Flag();
    bool GetMC2Flag();
    void SetHcclComm(const HcclComm &comm) { this->GetDeviceContext().SetHcclComm(comm); }
    HcclComm GetHcclComm() { return this->GetDeviceContext().GetHcclComm(); }
    bool GetLcclFlag();
    void SetLcclFlag(const bool &isLccl);

    /// 获取算子是否有mc2Ctx的标识，santizer的mc2算子定义和msprof op不同，目前此函数仅供sanitizer使用
    bool GetMc2CtxFlag() const;

    /// 解析二级指针地址信息，index表示位于args中的第几个入参
    void ParseSecondPtrAddrs(const rtArgsEx_t &argsInfo, OpMemInfo &opMemInfo, uint32_t index) const;

    /// 设置算子的argsSize
    void SetArgsSize(uint32_t argSize) { argsSize_ = argSize; }
    /// 获取算子的argsSize
    uint32_t GetArgsSize() const { return argsSize_; }

    /// 设置算子的argsSize
    void SetKernelParamNum(uint32_t kernelParamNum) { kernelParamNum_ = kernelParamNum; }
    /// 获取算子的argsSize
    uint32_t GetKernelParamNum() const { return kernelParamNum_; }

    void SetSimtUbDynamicSize(uint32_t simtUbDynamicSize) { simtUbDynamicSize_ = simtUbDynamicSize; }
    uint32_t GetSimtUbDynamicSize() const { return simtUbDynamicSize_; }

private:
    // 根据 stubFunc 查询 pcStart 地址
    bool GetPcStartAddr(StubFuncPtr stubFunc, uint64_t &pcStartAddr);

private:
    // used only by ut test to reset all information
    void Reset()
    {
        tempHostMemory_.clear();
        registerEvents_.clear();
        stubInfo_.clear();
        hdlToRegId_.clear();
        ClearArgsInfo();
        deviceContextMap_.clear();
        argsSize_ = 0;
        kernelParamNum_ = 0;
        simtUbDynamicSize_ = 0;
    }

    std::vector<void *> tempHostMemory_;
    std::vector<RegisterEvent> registerEvents_;
    // record each stubFunc
    std::unordered_map<const StubFunc*, StubFuncInfo> stubInfo_;
    std::unordered_map<const KernelHandle*, uint64_t> hdlToRegId_;
    mutable std::mutex mtx_;
    mutable std::unordered_map<int32_t, DeviceContext> deviceContextMap_;
    uint32_t argsSize_{};
    uint32_t kernelParamNum_{};
    uint32_t simtUbDynamicSize_{};
    bool hasSimt_{false};
};

#endif // __KERNEL_CONTEXT_H__
