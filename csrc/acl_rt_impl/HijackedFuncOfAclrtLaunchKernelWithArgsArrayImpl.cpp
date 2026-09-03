/* -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
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

// 该文件主要实现注入函数的功能，其配合被劫持函数的别名，实现新的劫持函数功能

#include <algorithm>
#include <vector>

#include "HijackedFunc.h"
#include "utils/InjectLogger.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/FuncSelector.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"
#include "runtime/RuntimeOrigin.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "runtime/inject_helpers/MemoryContext.h"
#include "runtime/inject_helpers/LaunchManager.h"
#include "runtime/inject_helpers/ArgsManager.h"
#include "runtime/inject_helpers/DevMemManager.h"
#include "runtime/inject_helpers/FuncManager.h"
#include "runtime/inject_helpers/InstrReport.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/BBCountDumper.h"
#include "runtime/inject_helpers/DBITask.h"
#include "runtime/inject_helpers/DbiRecordTaskHelper.h"
#include "runtime/inject_helpers/LaunchArgs.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/SyncStreamWithInterrupt.h"

HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl::HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtLaunchKernelWithArgsArrayImpl") {}

static void ReportKernelBinary(const RegisterContextSP &regCtx) {
    auto const &elfData = regCtx->GetElfData();
    PacketHead head{PacketType::KERNEL_BINARY};
    std::string buffer(elfData.cbegin(), elfData.cend());
    int32_t deviceId = 0;
    aclrtGetDeviceImplOrigin(&deviceId);
    LocalDevice::GetInstance(deviceId).Notify(Serialize(head, buffer.size()) + std::move(buffer));
}

bool HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl::InitParam(
    void *func, uint32_t numBlocks, aclrtStream stream, aclrtLaunchKernelCfg *cfg, void **args) {
    // 归一化入参 func：可能是核函数符号，统一转成 func handle，后续都用它
    aclrtFuncHandle resolvedFunc = ResolveFuncHandle(func);
    refreshParamFunc_ = [this, resolvedFunc, numBlocks, stream, cfg, args]() {
        funcHandle_ = resolvedFunc;
        numBlocks_ = numBlocks;
        stream_ = stream;
        cfg_ = cfg;
        newArgsCtx_ = nullptr;
        memInfo_ = nullptr;
        memSize_ = 0;
        devId_ = DeviceContext::GetRunningDeviceId();
        skipSanitizer_ = false;
    };
    refreshParamFunc_();
    if (FuncManager::Instance().GetContext(funcHandle_) == nullptr) {
        CreateFuncContext(funcHandle_);
    }
    auto funcCtx = FuncManager::Instance().GetContext(funcHandle_);
    if (funcCtx && funcCtx->GetRegisterContext()->GetMagic() == RT_DEV_BINARY_MAGIC_ELF_AICPU) {
        return false;
    }
    if (funcCtx == nullptr || !funcCtx->QueryParamInfo()) {
        DEBUG_LOG("Query kernel param info failed, stop hijack this launch.");
        return false;
    }
    argsCtx_ = ArgsManager::Instance().CreateContext(
        args, funcCtx->GetParamCount(), funcCtx->GetParamOffsets(), funcCtx->GetParamSizes());
    if (argsCtx_ == nullptr) {
        DEBUG_LOG("Pack args array failed, stop hijack this launch.");
        return false;
    }
    launchCtx_ = LaunchManager::Local().CreateContext(funcHandle_, numBlocks, stream, cfg, argsCtx_);
    if (launchCtx_ == nullptr) {
        DEBUG_LOG("Create launch context failed");
        return false;
    }
    funcCtx_ = launchCtx_->GetFuncContext();
    regId_ = funcCtx_->GetRegisterContext()->GetRegisterId();
    isSink_ = LaunchManager::GetOrCreateStreamInfo(stream).binded;
    if (IsOpProf()) {
        profObj_ = std::make_shared<ProfDataCollect>(launchCtx_);
    }
    bool needMemLengthInfo = (IsOpProf() && profObj_ && profObj_->IsNeedDumpContext()) || IsSanitizer();
    if (needMemLengthInfo) {
        auto &memInfo = LaunchManager::Local().GetCurrentMemInfo();
        launchCtx_->UpdateOpMemInfoByAdump(memInfo);
    }
    LaunchManager::Local().ArchiveMemInfo();
    return true;
}

void HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl::ProfPre(const std::function<bool(void)> &func,
    const std::function<void(const std::string &)> &bbCountTask, aclrtStream stm) {
    profObj_->ProfInit(nullptr, nullptr, false); // pc_start落盘txt文件
    profObj_->ProfData(stm, func);
    if (profObj_->IsBBCountNeedGen() && bbCountTask != nullptr) {
        refreshParamFunc_();
        bbCountTask(ProfDataCollect::GetAicoreOutputPath(devId_));
    }
}

void HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl::SanitizerPre() {
    // mssanitizer SIGINT 信号处理接管
    BindSigIntHandler();

    std::string kernelName = launchCtx_->GetFuncContext()->GetKernelName();
    skipSanitizer_ = SkipSanitizer(kernelName);
    if (skipSanitizer_) {
        return;
    }
    if (isSink_) {
        return;
    }
    ReportKernelSummary(launchCtx_);
    ReportKernelBinary(launchCtx_->GetFuncContext()->GetRegisterContext());
    memInfo_ = __sanitizer_init(numBlocks_);
    if (memInfo_ == nullptr) {
        return;
    }
    // 追加 memInfo 指针并重建参数数组
    uint32_t paramOffset = 0;
    newArgsCtx_ = argsCtx_->Clone();
    if (newArgsCtx_ == nullptr || !newArgsCtx_->ExpandArgs(&memInfo_, sizeof(uintptr_t), paramOffset)) {
        WARN_LOG("Append memInfo to args failed.");
        return;
    }
    DBITaskConfig::Instance().argsSize_ = paramOffset;
    DbiRecordTaskHelper::AppendHbmOutParamInfoToConfig();
    auto newFuncCtx = RunDBITask(launchCtx_);
    // 似乎动态插桩的argsHandle不需要更新funcHandle也能行，先这样吧
    if (newFuncCtx) {
        funcCtx_ = newFuncCtx;
        launchCtx_->SetDBIFuncCtx(funcCtx_);
        funcHandle_ = funcCtx_->GetFuncHandle();
    }
}
// 调优自定义插桩统一调用此函数
bool HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl::PrepareDbiTask(ProfDBIType mode, uint64_t memSize) {
    // 每次调用插桩前需要清理插桩用到的成员变量，保证不被上次插桩污染
    refreshParamFunc_();
    KernelMatcher::Config matchConfig;
    std::string path = GetEnv(DEVICE_PROF_DUMP_PATH_ENV);
    std::string pluginPath = ProfConfig::Instance().GetPluginPath(mode);
    std::vector<std::string> extraArgs;
    std::string tuneLogPath;
    DbiRecordTaskHelper::AppendExtraInfo(mode, ProfDataCollect::GetAicoreOutputPath(devId_), tuneLogPath, extraArgs);
    DbiRecordTaskHelper::AppendHbmOutParamInfoArgs(extraArgs);
    DBITaskConfig::Instance().Init(BIType::CUSTOMIZE, pluginPath, matchConfig, path, tuneLogPath, extraArgs);
    memSize_ = memSize;
    memInfo_ = InitMemory(memSize_);
    uint32_t paramOffset = 0;
    newArgsCtx_ = argsCtx_->Clone();
    if (memInfo_ == nullptr || newArgsCtx_ == nullptr ||
        !newArgsCtx_->ExpandArgs(&memInfo_, sizeof(uintptr_t), paramOffset)) {
        WARN_LOG("Stub run failed, because of ExpandArgs failed, dbi mode is %d", static_cast<uint32_t>(mode));
        return false;
    }
    DBITaskConfig::Instance().argsSize_ = paramOffset;
    auto newFuncCtx = RunDBITask(launchCtx_);
    if (newFuncCtx) {
        funcCtx_ = newFuncCtx;
        funcHandle_ = funcCtx_->GetFuncHandle();
        launchCtx_->SetDBIFuncCtx(funcCtx_);
        return true;
    }
    WARN_LOG("New function context get failed, dbi mode is %d", static_cast<uint32_t>(mode));
    return false;
}

void HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl::ProfPreForInstrProf(const std::function<bool(void)> &func,
    const std::function<void(const std::string &)> &bbCountTask, rtStream_t stream) {
    auto funcStub = [this]() {
        auto argsArrayCtx = std::static_pointer_cast<ArgsArrayContext>(
            newArgsCtx_ != nullptr ? newArgsCtx_ : argsCtx_);
        return (aclrtLaunchKernelWithArgsArrayImplOrigin(
                    funcHandle_, numBlocks_, stream_, cfg_, argsArrayCtx->GetArgsArray()) == ACL_SUCCESS);
    };
    if (profObj_->IsPCSamplingNeedGen() && launchCtx_->GetFuncContext()->GetRegisterContext()->HasSimtSymbol()) {
        if (PrepareDbiTask(ProfDBIType::INSTR_PROF_START, INSTR_PROF_MEMSIZE)) {
            profObj_->InstrProfData(stream, funcStub);
            profObj_->GenRecordData(memSize_, memInfo_, PCOFFSET_RECORD);
        }
        if (launchCtx_->GetDBIFuncCtx() == nullptr) {
            WARN_LOG("Failed to get pcsampling start pc");
        } else {
            auto kernelAddr = launchCtx_->GetDBIFuncCtx()->GetKernelPC();
            WriteStringToFile(JoinPath({ProfDataCollect::GetAicoreOutputPath(devId_), "pc_start_pcsampling.txt"}),
                NumToHexString(kernelAddr), std::fstream::out | std::fstream::binary);
        }
    }
    ProfDBIType timelineType;
    if (profObj_->IsTimelineNeedGen(timelineType)) {
        if (PrepareDbiTask(timelineType, INSTR_PROF_MEMSIZE)) {
            profObj_->InstrProfData(stream, funcStub);
        }
    }
    refreshParamFunc_();
    ProfPre(func, bbCountTask, stream);
}

void HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl::Pre(
    void *func, uint32_t numBlocks, aclrtStream stream, aclrtLaunchKernelCfg *cfg, void **args) {
    if (!InitParam(func, numBlocks, stream, cfg, args)) {
        DEBUG_LOG("Invalid param, stop hijack this launch.");
        return;
    }
    auto bbCountTask = [this](const std::string &outputPath = "") {
        DBITaskConfig::Instance().extraCompilerArgs_.clear();
        DBITaskConfig::Instance().argsSize_ = launchCtx_->GetArgsContext()->GetLastParamOffset();
        DbiRecordTaskHelper::AppendHbmOutParamInfoToConfig();
        auto stubCtx = BBCountDumper::Instance().Replace(launchCtx_, outputPath);
        if (stubCtx == nullptr) {
            return;
        }
        funcCtx_ = stubCtx;
        launchCtx_->SetDBIFuncCtx(funcCtx_);
        funcHandle_ = funcCtx_->GetFuncHandle();
        memSize_ = BBCountDumper::Instance().GetMemSize(regId_, outputPath);
        memInfo_ = InitMemory(memSize_);
        if (memInfo_ != nullptr && argsCtx_ != nullptr) {
            uint32_t paramOffset = 0;
            newArgsCtx_ = argsCtx_->Clone();
            if (newArgsCtx_ == nullptr || !newArgsCtx_->ExpandArgs(&memInfo_, sizeof(uintptr_t), paramOffset)) {
                WARN_LOG("bbCountTask expand args failed.");
                return;
            }
            DBITaskConfig::Instance().argsSize_ = paramOffset;
        }
    };
    if (IsOpProf() && profObj_) {
        if (ProfConfig::Instance().IsSimulator()) {
            profObj_->ProfInit(nullptr, nullptr, false); // 完全切换至aclrt接口时需要删除该函数入参
        } else {
            auto function = [func, numBlocks, stream, cfg, args]() {
                return (aclrtLaunchKernelWithArgsArrayImplOrigin(func, numBlocks, stream, cfg, args) == ACL_SUCCESS);
            };
            ProfPreForInstrProf(function, bbCountTask, stream);
        }
    }
    if (IsSanitizer()) {
        this->SanitizerPre();
    }
}
aclError HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl::Call(
    void *func, uint32_t numBlocks, aclrtStream stream, aclrtLaunchKernelCfg *cfg, void **args) {
    Pre(func, numBlocks, stream, cfg, args);
    if (originfunc_ == nullptr) {
        ERROR_LOG("%s Hijacked func pointer is nullptr.", __FUNCTION__);
        return EmptyFunc();
    }
    if (IsOpProf() && profObj_ && !profObj_->IsNeedRunOriginLaunch()) {
        return Post(ACL_ERROR_NONE);
    }
    // InitParam 失败时无可用参数数组，回退使用原始参数数组
    auto argsArrayCtx = std::static_pointer_cast<ArgsArrayContext>(
        newArgsCtx_ != nullptr ? newArgsCtx_ : argsCtx_);
    return Post(originfunc_(funcHandle_, numBlocks_, stream_, cfg_,
        (argsArrayCtx != nullptr && argsArrayCtx->GetArgsArray() != nullptr) ? argsArrayCtx->GetArgsArray() : args));
}

void HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl::SanitizerPost() {
    if (skipSanitizer_) {
        // 对于 <<<>>> 场景，编译器也会在算子调用符处插入 __sanitizer_finalize，因此为了防止
        // 编译器插入的 __sanitizer_finalize 生效，需要在此处将记录内存状态设置为失效
        DevMemManager::Instance().SetMemoryInitFlag(false);
    } else if (isSink_) {
        aclrtSynchronizeStreamImplOrigin(stream_);
        KernelDumper::Instance().LaunchDumpTask(stream_, true);
    } else if (memInfo_) {
        if (launchCtx_ == nullptr) {
            return;
        }

        // wait for kernel execution done, and catch potential exception
        SyncStreamWithInterrupt(stream_);

        auto argsArrayCtx = std::static_pointer_cast<ArgsArrayContext>(
            newArgsCtx_ != nullptr ? newArgsCtx_ : argsCtx_);
        AclrtLaunchArgsInfo launchInfo{};
        launchInfo.hostArgs = argsArrayCtx != nullptr ? argsArrayCtx->GetPackedArgs() : nullptr;
        launchInfo.argsSize = argsArrayCtx != nullptr ? argsArrayCtx->GetPackedArgsSize() : 0;
        launchInfo.placeHolderArray = nullptr;
        launchInfo.placeHolderNum = 0;
        // ArgsArray 路径无 placeholder，这里用 meta 解析出的 tilingParamsNo
        // 结合 ArgsArrayContext 的 paramOffsets_ 推算 tiling 指针在 hostArgs 中的偏移，
        // 供 ReportOpMallocInfo 上报 tiling 的 malloc/memset/free。
        auto &opMemInfo = LaunchManager::Local().GetCurrentMemInfo();
        if (argsArrayCtx != nullptr && opMemInfo.tilingParamsNo != 0U) {
            size_t tilingOffset = 0;
            if (argsArrayCtx->GetParamOffsetByParamsNo(opMemInfo.tilingParamsNo, tilingOffset)) {
                launchInfo.tilingAddrOffset = static_cast<uint32_t>(tilingOffset);
            }
        }
        ReportOpMallocInfo(launchInfo, opMemInfo);

        auto const &elfData = funcCtx_->GetRegisterContext()->GetElfData();
        std::map<std::string, Elf64_Shdr> headers;
        if (!GetSectionHeaders(elfData, headers)) {
            return;
        }

        if (!funcCtx_->isAiCpu) {
            auto allocHeaders = GetAllocSectionHeaders(headers);
            auto startPC = funcCtx_->GetStartPC();
            ReportSectionsMalloc(startPC, allocHeaders);
            __sanitizer_finalize(memInfo_, numBlocks_);
            ReportSectionsFree(startPC, allocHeaders);
        } else {
            __sanitizer_finalize(memInfo_, numBlocks_);
        }

        ReportOpFreeInfo(LaunchManager::Local().GetCurrentMemInfo());
        ExitAfterProcess();
    }
}

void HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl::RunDbiRecordTask(ProfDBIType mode, const char *failedLog) {
    if (!DbiRecordTaskHelper::IsNeedGen(profObj_.get(), mode)) {
        return;
    }
    aclrtSynchronizeStreamImplOrigin(stream_);
    // Call 已消耗过一次算子输入；每次 DBI 重放前恢复输入到快照态，
    // 否则后续插桩读到的是被污染的数据（app 模式 Call 跳过 origin，输入未被消耗，无需恢复）
    if (!ProfConfig::Instance().IsAppReplay() && !MemoryContext::Instance().Restore()) {
        WARN_LOG("Restore input data before DBI record task failed, mode=%u", static_cast<uint32_t>(mode));
    }
    uint64_t memSize = DbiRecordTaskHelper::GetDbiRecordMemSize(mode, numBlocks_);
    if (!PrepareDbiTask(mode, memSize) || originfunc_ == nullptr) {
        return;
    }
    auto argsArrayCtx = std::static_pointer_cast<ArgsArrayContext>(
        newArgsCtx_ != nullptr ? newArgsCtx_ : argsCtx_);
    originfunc_(
        funcHandle_, numBlocks_, stream_, cfg_, argsArrayCtx != nullptr ? argsArrayCtx->GetArgsArray() : nullptr);
    aclError ret = aclrtSynchronizeStreamImplOrigin(stream_);
    if (ret == ACL_SUCCESS) {
        DbiRecordTaskHelper::CollectData(profObj_.get(), mode, memSize_, memInfo_);
        return;
    }
    WARN_LOG("%s", failedLog);
}

void HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl::ProfPost() {
    if (profObj_->IsBBCountNeedGen()) {
        aclrtSynchronizeStreamImplOrigin(stream_);
        profObj_->GenBBcountFile(regId_, memSize_, memInfo_);
    }
    for (const auto &task : DbiRecordTaskHelper::DBI_RECORD_TASKS) {
        RunDbiRecordTask(task.mode, task.aclFailedLog);
    }
    profObj_->PostProcess();
}

aclError HijackedFuncOfAclrtLaunchKernelWithArgsArrayImpl::Post(aclError ret) {
    if (ret != ACL_SUCCESS) {
        return ret;
    }
    if (launchCtx_ == nullptr) {
        return ret;
    }
    if (IsSanitizer()) {
        SanitizerPost();
    }
    if (IsOpProf() && profObj_) {
        if (ProfConfig::Instance().IsSimulator()) {
            aclrtSynchronizeStreamImplOrigin(stream_);
            profObj_->ProfData();
        } else {
            ProfPost();
        }
    }
    DevMemManager::Instance().Free();
    return ret;
}
