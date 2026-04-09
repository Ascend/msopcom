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

// 该文件主要实现注入函数的功能，其配合被劫持函数的别名，实现新的劫持函数功能

#include "HijackedFunc.h"
#include "utils/InjectLogger.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/FuncSelector.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "runtime/inject_helpers/MemoryContext.h"
#include "runtime/inject_helpers/LaunchManager.h"
#include "runtime/inject_helpers/ArgsRawContext.h"
#include "runtime/inject_helpers/ArgsManager.h"
#include "runtime/inject_helpers/DevMemManager.h"
#include "runtime/inject_helpers/FuncManager.h"
#include "runtime/inject_helpers/InstrReport.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/BBCountDumper.h"
#include "runtime/inject_helpers/DBITask.h"
#include "runtime/inject_helpers/LaunchArgs.h"

namespace {

void ReportKernelBinary(RegisterContextSP regCtx)
{
    auto const &elfData = regCtx->GetElfData();
    PacketHead head { PacketType::KERNEL_BINARY };
    std::string buffer(elfData.cbegin(), elfData.cend());
    LocalDevice::Local().Notify(Serialize(head, buffer.size()) + std::move(buffer));
}

} // namespace [Dummy]

HijackedFuncOfAclrtLaunchKernelImpl::HijackedFuncOfAclrtLaunchKernelImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtLaunchKernelImpl") {}

bool HijackedFuncOfAclrtLaunchKernelImpl::InitParam(aclrtFuncHandle funcHandle, uint32_t blockDim,
                                                    const void *argsData, size_t argsSize, aclrtStream stream)
{
    refreshParamFunc_ = [this, funcHandle, blockDim, argsData, argsSize, stream]() {
        funcHandle_ = funcHandle;
        blockDim_ = blockDim;
        argsSize_ = argsSize;
        stream_ = stream;
        argsData_ = nullptr;
        devId_ = DeviceContext::GetRunningDeviceId();
        skipSanitizer_ = false;
    };
    refreshParamFunc_();
    argsCtx_ = ArgsManager::Instance().CreateContext(const_cast<void *>(argsData), argsSize, true);
    launchCtx_ = LaunchManager::Local().CreateContext(funcHandle, blockDim, stream, nullptr, argsCtx_);
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

// 调优自定义插桩统一调用此函数 
bool HijackedFuncOfAclrtLaunchKernelImpl::PrepareDbiTaskForInstrProf(ProfDBIType mode, uint64_t memSize)
{
    // 每次调用插桩前需要清理插桩用到的成员变量，保证不被上次插桩污染
    refreshParamFunc_();
    KernelMatcher::Config matchConfig;
    std::string path = GetEnv(DEVICE_PROF_DUMP_PATH_ENV);
    std::string pluginPath = ProfConfig::Instance().GetPluginPath(mode);
 	std::vector<std::string> extraArgs = (mode == ProfDBIType::INSTR_PROF_START) ? std::vector<std::string>{START_STUB_COMPILER_ARGS} :
 	    std::vector<std::string>();
    DBITaskConfig::Instance().Init(BIType::CUSTOMIZE, pluginPath, matchConfig, path, extraArgs);
    newArgsCtx_ = launchCtx_->GetArgsContext()->Clone();
    memSize_ = memSize;
    memInfo_ = InitMemory(memSize_);
    if (memInfo_ == nullptr || newArgsCtx_ == nullptr ||
        !newArgsCtx_->ExpandArgs(&memInfo_, sizeof(uintptr_t), DBITaskConfig::Instance().argsSize_)) {
        WARN_LOG("Stub run failed, because of ExpandArgs failed, dbi mode is %d", static_cast<uint32_t>(mode));
        return false;
    }
    auto argsRawCtx = std::static_pointer_cast<ArgsRawContext>(newArgsCtx_);
    auto newFuncCtx = RunDBITask(launchCtx_);
    if (newFuncCtx) {
        funcCtx_ = newFuncCtx;
        funcHandle_ = funcCtx_->GetFuncHandle();
        launchCtx_->SetDBIFuncCtx(funcCtx_);
        argsData_ = argsRawCtx->GetArgs();
        argsSize_ = argsRawCtx->GetArgsSize();
        return true;
    }
    WARN_LOG("New function context get failed, dbi mode is %d", static_cast<uint32_t>(mode));
    return false;
}

void HijackedFuncOfAclrtLaunchKernelImpl::ProfPreForInstrProf(const std::function<bool(void)> &func,
    const std::function<void(const std::string &)> &bbCountTask, rtStream_t stream)
{
    auto funcStub = [this]() {
        return (aclrtLaunchKernelImplOrigin(funcHandle_, blockDim_, argsData_, argsSize_, stream_) == ACL_SUCCESS);
    };
    if (ProfConfig::Instance().IsPCSamplingEnabled() && launchCtx_->GetFuncContext()->GetRegisterContext()->HasSimtSymbol()) {
        if (PrepareDbiTaskForInstrProf(ProfDBIType::INSTR_PROF_START, INSTR_PROF_MEMSIZE)) {
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
    if (ProfConfig::Instance().IsTimelineEnabled()) {
        PrepareDbiTaskForInstrProf(ProfDBIType::INSTR_PROF_END, INSTR_PROF_MEMSIZE);
        profObj_->InstrProfData(stream, funcStub);
    }
    ProfPre(func, bbCountTask, stream);
}

void HijackedFuncOfAclrtLaunchKernelImpl::ProfPre(const std::function<bool(void)> &func,
                                                  const std::function<void(const std::string &)> &bbCountTask,
                                                  aclrtStream stm)
{
    profObj_->ProfInit(nullptr, nullptr, false); // pc_start落盘txt文件
    profObj_->ProfData(stm, func);
    if (profObj_->IsBBCountNeedGen() && bbCountTask != nullptr) {
        refreshParamFunc_();
        bbCountTask(ProfDataCollect::GetAicoreOutputPath(devId_));
    }
}

void HijackedFuncOfAclrtLaunchKernelImpl::SanitizerPre()
{
    std::string kernelName = launchCtx_->GetFuncContext()->GetKernelName();
    skipSanitizer_ = SkipSanitizer(kernelName);
    DevMemManager::Instance().SetSkipKernelFlag(this->skipSanitizer_);
    if (!skipSanitizer_) {
        if (isSink_) { return; }
        ReportKernelSummary(launchCtx_);
        ReportKernelBinary(launchCtx_->GetFuncContext()->GetRegisterContext());
    }
    memInfo_ = __sanitizer_init(blockDim_);
    if (memInfo_ == nullptr) {
        return;
    }
    // expand args
    auto argsCtx = launchCtx_->GetArgsContext();
    if (!argsCtx->ExpandArgs(&memInfo_, sizeof(uintptr_t), DBITaskConfig::Instance().argsSize_)) {
        WARN_LOG("Expand sanitizer kernel args failed.");
        return;
    }
    auto argsRawCtx = std::static_pointer_cast<ArgsRawContext>(argsCtx);
    argsData_ = argsRawCtx->GetArgs();
    argsSize_ = argsRawCtx->GetArgsSize();
    auto newFuncCtx = RunDBITask(launchCtx_);
    // 似乎动态插桩的argsHandle不需要更新funcHandle也能行，先这样吧
    if (newFuncCtx) {
        funcCtx_ = newFuncCtx;
        launchCtx_->SetDBIFuncCtx(funcCtx_);
        funcHandle_ = funcCtx_->GetFuncHandle();
    }
}

void HijackedFuncOfAclrtLaunchKernelImpl::Pre(aclrtFuncHandle funcHandle, uint32_t blockDim, const void *argsData,
                                              size_t argsSize, aclrtStream stream)
{
    if (!InitParam(funcHandle, blockDim, argsData, argsSize, stream)) {
        DEBUG_LOG("Invalid param, stop hijack this launch.");
        return;
    }
    auto bbCountTask = [this](const std::string &outputPath = "") {
        DBITaskConfig::Instance().argsSize_ = launchCtx_->GetArgsContext()->GetLastParamOffset();
        auto stubCtx = BBCountDumper::Instance().Replace(launchCtx_, outputPath);
        if (stubCtx == nullptr) {
            return;
        }
        funcCtx_ = stubCtx;
        launchCtx_->SetDBIFuncCtx(funcCtx_);
        funcHandle_ = funcCtx_->GetFuncHandle();
        memSize_ = BBCountDumper::Instance().GetMemSize(regId_, outputPath);
        memInfo_ = InitMemory(memSize_);
        if (memInfo_ != nullptr) {
            newArgsCtx_ = launchCtx_->GetArgsContext()->Clone();
            newArgsCtx_->ExpandArgs(&memInfo_, sizeof(uintptr_t), DBITaskConfig::Instance().argsSize_);
            auto argsRawCtx = std::static_pointer_cast<ArgsRawContext>(newArgsCtx_);
            argsData_ = argsRawCtx->GetArgs();
            argsSize_ = argsRawCtx->GetArgsSize();
        }
    };
    if (IsOpProf()) {
        if (ProfConfig::Instance().IsSimulator()) {
            profObj_->ProfInit(nullptr, nullptr, false); // 完全切换至aclrt接口时需要删除该函数入参
        } else {
            auto func = [funcHandle, blockDim, argsData, argsSize, stream]() {
                return (aclrtLaunchKernelImplOrigin(funcHandle, blockDim, argsData, argsSize, stream) == ACL_SUCCESS);
            };
            ProfPreForInstrProf(func, bbCountTask, stream);
        }
    }
    if (IsSanitizer()) {
        this->SanitizerPre();
    }
}

aclError HijackedFuncOfAclrtLaunchKernelImpl::Call(aclrtFuncHandle funcHandle, uint32_t blockDim, const void *argsData,
                                                   size_t argsSize, aclrtStream stream)
{
    Pre(funcHandle, blockDim, argsData, argsSize, stream);
    if (originfunc_ == nullptr) {
        ERROR_LOG("%s Hijacked func pointer is nullptr.", __FUNCTION__);
        return EmptyFunc();
    }
    if (IsOpProf() && profObj_ && !profObj_->IsNeedRunOriginLaunch()) {
        return Post(ACL_ERROR_NONE);
    }
    if (argsData_ != nullptr) {
        return Post(originfunc_(funcHandle_, blockDim, argsData_, argsSize_, stream));
    }
    return Post(originfunc_(funcHandle, blockDim, argsData, argsSize, stream));
}

void HijackedFuncOfAclrtLaunchKernelImpl::SanitizerPost()
{
    if (skipSanitizer_) {
        // 对于 <<<>>> 场景，编译器也会在算子调用符处插入 __sanitizer_finalize，因此为了防止
        // 编译器插入的 __sanitizer_finalize 生效，需要在此处将记录内存状态设置为失效
        DevMemManager::Instance().SetMemoryInitFlag(false);
    } else if (isSink_) {
        rtStreamSynchronizeOrigin(stream_);
        KernelDumper::Instance().LaunchDumpTask(stream_, true);
    } else if (memInfo_) {
        if (launchCtx_ == nullptr) {
            return;
        }

        // wait for kernel execution done, and catch potential exception
        rtStreamSynchronizeOrigin(stream_);

        auto const &elfData = funcCtx_->GetRegisterContext()->GetElfData();
        std::map<std::string, Elf64_Shdr> headers;
        if (!GetSectionHeaders(elfData, headers)) {
            return;
        }

        auto allocHeaders = GetAllocSectionHeaders(headers);
        auto startPC = funcCtx_->GetStartPC();
        ReportSectionsMalloc(startPC, allocHeaders);
        __sanitizer_finalize(memInfo_, blockDim_);
        ReportSectionsFree(startPC, allocHeaders);
    }
}

void HijackedFuncOfAclrtLaunchKernelImpl::DoOperandRecord()
{
    std::string socVersion = DeviceContext::Local().GetSocVersion();
    if (!profObj_->IsOperandRecordNeedGen(socVersion)) {
        return;
    }
    aclrtSynchronizeStreamImplOrigin(stream_);
    uint64_t sizePerAllType = static_cast<uint32_t>(OperandType::END) * sizeof(OperandRecord) + SIMT_THREAD_GAP;
    uint64_t memSize = sizeof(OperandHeader) + (sizePerAllType * (MAX_THREAD_NUM + 1) + BLOCK_GAP) *  GetCoreNumForDbi(blockDim_);
    if (PrepareDbiTaskForInstrProf(ProfDBIType::OPERAND_RECORD, memSize) && originfunc_ != nullptr) {
        originfunc_(funcHandle_, blockDim_, argsData_, argsSize_, stream_);
        aclError ret = aclrtSynchronizeStreamImplOrigin(stream_);
        if (ret == ACL_SUCCESS) {
            profObj_->GenRecordData(memSize_, memInfo_, OPERAND_RECORD);
        } else {
            WARN_LOG("Run operand record func failed");
        }
    }
    memInfo_ = nullptr;
}

void HijackedFuncOfAclrtLaunchKernelImpl::ProfPost()
{
    if (profObj_->IsBBCountNeedGen()) {
        aclrtSynchronizeStreamImplOrigin(stream_);
        profObj_->GenBBcountFile(regId_, memSize_, memInfo_);
    }
    if (profObj_->IsMemoryChartNeedGen()) {
        aclrtSynchronizeStreamImplOrigin(stream_);
        uint64_t memSize = BLOCK_MEM_SIZE * GetCoreNumForDbi(blockDim_);
        if (PrepareDbiTaskForInstrProf(ProfDBIType::MEMORY_CHART, memSize) && originfunc_ != nullptr) {
            originfunc_(funcHandle_, blockDim_, argsData_, argsSize_, stream_);
            aclError ret = aclrtSynchronizeStreamImplOrigin(stream_);
            if (ret == ACL_SUCCESS) {
                profObj_->GenDBIData(memSize_, memInfo_);
            } else {
                WARN_LOG("Run dbi func failed");
            }
        }
        memInfo_ = nullptr;
    }
    DoOperandRecord();
    profObj_->PostProcess();
}

aclError HijackedFuncOfAclrtLaunchKernelImpl::Post(aclError ret)
{
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
