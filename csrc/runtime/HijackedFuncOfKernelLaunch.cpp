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


#include "HijackedFunc.h"

#include <algorithm>
#include <string>
#include "RuntimeConfig.h"
#include "RuntimeOrigin.h"
#include "core/FuncSelector.h"
#include "inject_helpers/DevMemManager.h"
#include "inject_helpers/KernelContext.h"
#include "utils/InjectLogger.h"
#include "utils/Future.h"
#include "runtime/inject_helpers/BBCountDumper.h"
#include "runtime/inject_helpers/ConfigManager.h"
#include "runtime/inject_helpers/DBITask.h"
#include "runtime/inject_helpers/DevMemManager.h"
#include "runtime/inject_helpers/InstrReport.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/KernelMatcher.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/LaunchArgs.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "utils/FileSystem.h"

using namespace std;

namespace {
} // namespace [Dummy]

HijackedFuncOfKernelLaunch::HijackedFuncOfKernelLaunch()
    : HijackedFuncOfKernelLaunch::HijackedFunc(
    std::string(RuntimeLibName()), std::string("rtKernelLaunch")) { }

void HijackedFuncOfKernelLaunch::InitParam(const void *stubFunc, uint32_t blockDim, void *args, uint32_t argsSize,
                                           rtSmDesc_t *smDesc, rtStream_t stm)
{
    refreshParamFunc_ = [this, stubFunc, blockDim, args, argsSize, smDesc, stm]() {
        this->stubFunc_ = stubFunc;
        this->blockDim_ = blockDim;
        this->args_ = args;
        this->argsSize_ = argsSize;
        this->memInfo_ = nullptr;
        this->memSize_ = 0;
        this->stm_ = stm;
        this->argsVec_.clear();
        this->smDesc_ = smDesc;
    };
    refreshParamFunc_();
    devId_ = DeviceContext::GetRunningDeviceId();
    KernelContext::Instance().AddLaunchEvent(stubFunc, blockDim, nullptr, stm);
    this->launchId_ = KernelContext::Instance().GetLaunchId();
    this->regId_ = KernelContext::Instance().GetRegisterId(this->launchId_);
    KernelContext::LaunchEvent event;
    KernelContext::Instance().GetLastLaunchEvent(event);
    this->isSink_ = event.isSink;
    KernelContext::Instance().SetArgsSize(argsSize);
    KernelContext::Instance().SetKernelParamNum(argsSize);
    if (IsOpProf()) {
        this->profObj_ = MakeShared<ProfDataCollect>();
    }
    auto alignSize = GetAlignSize(argsSize);
    DBITaskConfig::Instance().argsSize_ = alignSize;  // other rtKernel Launch type calculate by bisheng-tune.
    rtDevBinary_t binary;
    bool binaryGetSuccess = KernelContext::Instance().GetDevBinary(KernelContext::KernelHandlePtr{event.hdl}, binary);
    bool needMemLengthInfo = (IsOpProf() && profObj_ && profObj_->IsNeedDumpContext()) || IsSanitizer();
    if (binaryGetSuccess && needMemLengthInfo) {
        KernelContext::Instance().ParseMetaDataFromBinary(binary);
    }
    KernelContext::Instance().ArchiveMemInfo();
}

void HijackedFuncOfKernelLaunch::ProfPreForInstrProf(const std::function<bool(void)> &func,
                                                     const std::function<void(const std::string &)> &bbCountTask,
                                                     rtStream_t stm)
{
    auto funcStub = [this]() {
        return (rtKernelLaunchOrigin(this->stubFunc_, this->blockDim_, this->args_, this->argsSize_,
                                    this->smDesc_, this->stm_) == RT_ERROR_NONE);
    };
    if (ProfConfig::Instance().IsPCSamplingEnabled() && KernelContext::Instance().HasSimtSymbol()) {
        if (PrepareDbiTaskForInstrProf(ProfDBIType::INSTR_PROF_START, INSTR_PROF_MEMSIZE)) {
            KernelContext::StubFuncPtr stubFuncPtr{this->stubFunc_};
            uint64_t kernelAddr;
            if (!KernelContext::Instance().GetDeviceContext()
                .GetKernelAddr(KernelContext::StubFuncArgs{stubFuncPtr.value, nullptr}, kernelAddr)) {
                WARN_LOG("Can not get kernel addr for kernel start stub.");
            }
            WriteStringToFile(JoinPath({ProfDataCollect::GetAicoreOutputPath(devId_), "pc_start_pcsampling.txt"}),
                NumToHexString(kernelAddr), std::fstream::out | std::fstream::binary);
            profObj_->InstrProfData(stm, funcStub);
            profObj_->GenRecordData(memSize_, memInfo_, PCOFFSET_RECORD);
        }
    }
    if (ProfConfig::Instance().IsTimelineEnabled()) {
        PrepareDbiTaskForInstrProf(ProfDBIType::INSTR_PROF_END, INSTR_PROF_MEMSIZE);
        profObj_->InstrProfData(stm, funcStub);
    }
    ProfPre(func, bbCountTask, stm);
}

void HijackedFuncOfKernelLaunch::ProfPre(const std::function<bool(void)> &func,
                                         const std::function<void(const std::string &)> &bbCountTask,
                                         rtStream_t stm)
{
    KernelContext::LaunchEvent event;
    KernelContext::Instance().GetLaunchEvent(launchId_, event);
    profObj_->ProfInit(event.hdl, event.stubFunc); // pc_start落盘txt文件
    profObj_->ProfData(stm, func);
    if (profObj_->IsBBCountNeedGen()) {
        refreshParamFunc_();
        bbCountTask(ProfDataCollect::GetAicoreOutputPath(devId_));
    }
}

void HijackedFuncOfKernelLaunch::SanitizerPre()
{
    std::string kernelName = KernelContext::Instance().GetLaunchName();
    this->skipSanitizer_ = SkipSanitizer(kernelName);
    DevMemManager::Instance().SetSkipKernelFlag(this->skipSanitizer_);
    if (!this->skipSanitizer_) {
        if (isSink_) { return; }
        ReportKernelSummary(launchId_);
        KernelContext::Instance().ReportKernelBinary(KernelContext::StubFuncPtr{this->stubFunc_});
        RunDBITask(&this->stubFunc_);
        KernelContext::LaunchEvent event;
        if (!KernelContext::Instance().GetLastLaunchEvent(event)) { return; }

        rtDevBinary_t binary;
        KernelContext::KernelHandlePtr hdl{event.hdl};
        if (!KernelContext::Instance().GetDevBinary(hdl, binary, true) &&
            !KernelContext::Instance().GetDevBinary(hdl, binary, false)) { return; }

        std::map<std::string, Elf64_Shdr> headers;
        if (!GetSectionHeaders(binary, headers)) {
            return;
        }

        sections_ = GetAllocSectionHeaders(headers);
        ReportSectionsMalloc(event.pcStartAddr, sections_);
        ReportOverflowMalloc(KernelContext::Instance().GetOpMemInfo());
    }
    this->memInfo_ = __sanitizer_init(this->blockDim_);
    if (this->memInfo_) {
        ExpandArgs(this->args_, this->argsSize_, this->argsVec_, this->memInfo_);
    }
}

void HijackedFuncOfKernelLaunch::ProfPost()
{
    if (profObj_->IsBBCountNeedGen()) {
        rtError_t bbLaunchRet = rtStreamSynchronizeOrigin(this->stm_);
        if (bbLaunchRet != RT_ERROR_NONE) {
            WARN_LOG("BB count kernel launch failed, ret is %d.", bbLaunchRet);
        } else {
            profObj_->GenBBcountFile(regId_, this->memSize_, this->memInfo_);
        }
    }
    if (profObj_->IsMemoryChartNeedGen()) {
        rtStreamSynchronizeOrigin(stm_);
        uint64_t memSize = BLOCK_MEM_SIZE * GetCoreNumForDbi(blockDim_);
        if (PrepareDbiTaskForInstrProf(ProfDBIType::MEMORY_CHART, memSize) && originfunc_ != nullptr) {
            originfunc_(stubFunc_, blockDim_, args_, argsSize_, smDesc_, stm_);
            rtError_t memchartLaunchRet = rtStreamSynchronizeOrigin(this->stm_);
            if (memchartLaunchRet != RT_ERROR_NONE) {
                WARN_LOG("Memory chart kernel launch failed, ret is %d.", memchartLaunchRet);
            } else {
                profObj_->GenDBIData(memSize_, memInfo_);
            }
        }
    }
    std::string socVersion = DeviceContext::Local().GetSocVersion();
    if (profObj_->IsOperandRecordNeedGen(socVersion)) {
        rtStreamSynchronizeOrigin(stm_);
        uint64_t sizePerAllType = static_cast<uint32_t>(OperandType::END) * sizeof(OperandRecord) + SIMT_THREAD_GAP;
        uint64_t memSize = sizeof(OperandHeader) + (sizePerAllType * (MAX_THREAD_NUM + 1) + BLOCK_GAP) *  GetCoreNumForDbi(blockDim_);
        if (PrepareDbiTaskForInstrProf(ProfDBIType::OPERAND_RECORD, memSize) && originfunc_ != nullptr) {
            originfunc_(stubFunc_, blockDim_, args_, argsSize_, smDesc_, stm_);
            rtError_t opRecordLaunchRet = rtStreamSynchronizeOrigin(this->stm_);
            if (opRecordLaunchRet != RT_ERROR_NONE) {
                WARN_LOG("Operand record kernel launch failed, ret is %d.", opRecordLaunchRet);
            } else {
                profObj_->GenRecordData(memSize_, memInfo_, OPERAND_RECORD);
            }
        }
    }
    profObj_->PostProcess();
}

void HijackedFuncOfKernelLaunch::SanitizerPost() const
{
    if (this->skipSanitizer_) {
        // 对于 <<<>>> 场景，编译器也会在算子调用符处插入 __sanitizer_finalize，因此为了防止
        // 编译器插入的 __sanitizer_finalize 生效，需要在此处将记录内存状态设置为失效
        DevMemManager::Instance().SetMemoryInitFlag(false);
    } else if (isSink_) {
        rtStreamSynchronizeOrigin(this->stm_);
        KernelDumper::Instance().LaunchDumpTask(stm_);
    } else if (this->memInfo_) {
        // wait for kernel execution done, and catch potential exception
        rtStreamSynchronizeOrigin(this->stm_);

        KernelContext::LaunchEvent event;
        if (!KernelContext::Instance().GetLastLaunchEvent(event)) {
            return;
        }

        __sanitizer_finalize(this->memInfo_, this->blockDim_);
        ReportSectionsFree(event.pcStartAddr, sections_);
        ReportOverflowFree(KernelContext::Instance().GetOpMemInfo());
    }
}

void HijackedFuncOfKernelLaunch::Pre(const void *stubFunc, uint32_t blockDim, void *args,
    uint32_t argsSize, rtSmDesc_t *smDesc, rtStream_t stm)
{
    InitParam(stubFunc, blockDim, args, argsSize, smDesc, stm);
    auto bbCountTask = [this](const std::string &outputPath = "") {
        if (BBCountDumper::Instance().Replace(&stubFunc_, launchId_, outputPath)) {
            memSize_ = BBCountDumper::Instance().GetMemSize(regId_, outputPath);
            memInfo_ = InitMemory(memSize_);
            if (memInfo_ != nullptr) {
                ExpandArgs(args_, argsSize_, argsVec_, memInfo_);
            }
        }
    };
    if (IsOpProf()) {
        if (ProfConfig::Instance().IsSimulator()) {
            KernelContext::RegisterEvent event;
            KernelContext::Instance().GetRegisterEvent(regId_, event);
            profObj_->ProfInit(event.hdl, stubFunc);
        } else {
            auto func = [stubFunc, blockDim, args, argsSize, smDesc, stm]() {
                return (rtKernelLaunchOrigin(stubFunc, blockDim, args, argsSize, smDesc, stm) == RT_ERROR_NONE);
            };
            ProfPreForInstrProf(func, bbCountTask, stm);
        }
    }

    if (IsSanitizer()) {
        SanitizerPre();
    }
}

// 调优自定义插桩统一调用此函数 
bool HijackedFuncOfKernelLaunch::PrepareDbiTaskForInstrProf(ProfDBIType mode, uint64_t memSize)
{
    // 每次调用插桩前需要清理插桩用到的成员变量，保证不被上次插桩污染
    refreshParamFunc_();
    KernelMatcher::Config matchConfig;
    std::string path = GetEnv(DEVICE_PROF_DUMP_PATH_ENV);
    std::string pluginPath = ProfConfig::Instance().GetPluginPath(mode);
    std::vector<std::string> extraArgs = (mode == ProfDBIType::INSTR_PROF_START) ? std::vector<std::string>{START_STUB_COMPILER_ARGS} :
        std::vector<std::string>();
    DBITaskConfig::Instance().Init(BIType::CUSTOMIZE, pluginPath, matchConfig, path, extraArgs);
    memSize_ = memSize;
    memInfo_ = InitMemory(memSize_);
    if (!ExpandArgs(this->args_, this->argsSize_, this->argsVec_, memInfo_) || !RunDBITask(&this->stubFunc_)) {
        ERROR_LOG("Stub run failed, dbi mode is %d", static_cast<uint32_t>(mode));
        return false;
    }
    return true;
}

rtError_t HijackedFuncOfKernelLaunch::Call(const void *stubFunc, uint32_t blockDim, void *args,
    uint32_t argsSize, rtSmDesc_t *smDesc, rtStream_t stm)
{
    Pre(stubFunc, blockDim, args, argsSize, smDesc, stm);
    if (this->originfunc_ == nullptr) {
        ERROR_LOG("rtKernelLaunch Hijacked func pointer is nullptr.");
        return RT_ERROR_RESERVED;
    }
    if (IsOpProf() && profObj_ && !profObj_->IsNeedRunOriginLaunch()) {
        return Post(RT_ERROR_NONE);
    }
    return Post(this->originfunc_(this->stubFunc_, blockDim, this->args_, this->argsSize_, smDesc, stm));
}

rtError_t HijackedFuncOfKernelLaunch::Post(rtError_t ret)
{
    // 获得文件名称，将拷贝出的信息落盘
    if (IsSanitizer()) {
        SanitizerPost();
    }
    if (IsOpProf() && profObj_) {
        if (ProfConfig::Instance().IsSimulator()) {
            rtStreamSynchronizeOrigin(this->stm_);
            profObj_->ProfData();
        } else {
            ProfPost();
        }
    }
    KernelContext::Instance().ClearArgsInfo();
    DevMemManager::Instance().Free();
    return ret;
}
