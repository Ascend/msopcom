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


#include <algorithm>
#include <elf.h>
#include <string>

#include "HijackedFunc.h"
#include "RuntimeOrigin.h"
#include "RuntimeConfig.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "utils/Future.h"
#include "utils/FileSystem.h"
#include "runtime/inject_helpers/BBCountDumper.h"
#include "runtime/inject_helpers/ConfigManager.h"
#include "runtime/inject_helpers/DBITask.h"
#include "runtime/inject_helpers/InstrReport.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/LaunchArgs.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "runtime/inject_helpers/DevMemManager.h"

using namespace std;

HijackedFuncOfKernelLaunchWithFlagV2::HijackedFuncOfKernelLaunchWithFlagV2()
    : HijackedFuncOfKernelLaunchWithFlagV2::HijackedFuncType(
    std::string(RuntimeLibName()), std::string("rtKernelLaunchWithFlagV2")) {}

void HijackedFuncOfKernelLaunchWithFlagV2::InitParam(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
    rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags,
    const rtTaskCfgInfo_t *cfgInfo)
{
    refreshParamFunc_ = [this, stubFunc, blockDim, argsInfo, smDesc, stm, flags, cfgInfo]() {
        this->stubFunc_ = stubFunc;
        this->blockDim_ = blockDim;
        this->argsInfo_ = argsInfo;
        this->memInfo_ = nullptr;
        this->memSize_ = 0;
        this->stm_ = stm;
        this->argsVec_.clear();
        this->newArgsInfo_ = *argsInfo;
        this->smDesc_ = smDesc;
        this->flags_ = flags;
        this->cfgInfo_ = cfgInfo;
        hostInput_.clear();
    };
    refreshParamFunc_();
    devId_ = DeviceContext::GetRunningDeviceId();
    KernelContext::Instance().AddLaunchEvent(stubFunc, blockDim, argsInfo, stm);
    this->launchId_ = KernelContext::Instance().GetLaunchId();
    this->regId_ = KernelContext::Instance().GetRegisterId(launchId_);
    KernelContext::LaunchEvent event;
    KernelContext::Instance().GetLaunchEvent(launchId_, event);
    this->isSink_ = event.isSink;
    if (argsInfo != nullptr) { KernelContext::Instance().SetArgsSize(argsInfo->argsSize); }
    if (IsSanitizer()) {
        if (cfgInfo != nullptr) { KernelContext::Instance().SetSimtUbDynamicSize(cfgInfo->localMemorySize); }
        KernelContext::Instance().SetKernelParamNum(GetKernelParamNum(argsInfo));
    }
    DBITaskConfig::Instance().argsSize_ = 0;  // avoid multi kernelLaunch,reset invalid argSize=0
    if (IsOpProf()) {
        this->profObj_ = MakeShared<ProfDataCollect>();
    }
    rtDevBinary_t binary;
    bool binaryGetSuccess = KernelContext::Instance().GetDevBinary(KernelContext::KernelHandlePtr{event.hdl}, binary);
    bool needMemLengthInfo = (IsOpProf() && profObj_->IsNeedDumpContext()) || IsSanitizer();
    if (binaryGetSuccess && needMemLengthInfo) {
        KernelContext::Instance().ParseMetaDataFromBinary(binary, argsInfo);
    }
    KernelContext::Instance().ArchiveMemInfo();
}
void HijackedFuncOfKernelLaunchWithFlagV2::ProfPre(const std::function<bool(void)> &func,
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

void HijackedFuncOfKernelLaunchWithFlagV2::SanitizerPre()
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
        KernelContext::Instance().GetLaunchEvent(launchId_, event);

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
        auto &opMemInfo = KernelContext::Instance().GetOpMemInfo();
        ReportOverflowMalloc(opMemInfo);
        /// 入队算子输入个数和tiling
        MemoryManage::Instance().CacheMemoryCount(opMemInfo.inputParamsAddrInfos.size() + 1);
        /// 入队空的extra触发MemoryManage类的内存筛选
        if (opMemInfo.inputParamsAddrInfos.size() > 0) {
            MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(0x0,
                opMemInfo.inputParamsAddrInfos[0].memInfoSrc, 0x0, false);
        }
    }
    if ((this->memInfo_ = __sanitizer_init(this->blockDim_))) {
        ExpandArgs(&this->newArgsInfo_, this->argsVec_, this->memInfo_, hostInput_, DBITaskConfig::Instance().argsSize_);
    }
}

void HijackedFuncOfKernelLaunchWithFlagV2::ProfPost()
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
            originfunc_(stubFunc_, blockDim_, &newArgsInfo_, smDesc_, stm_, flags_, cfgInfo_);
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
        uint64_t memSize = sizeof(OperandHeader) + (sizePerAllType * (MAX_THREAD_NUM + 1) + BLOCK_GAP) * GetCoreNumForDbi(blockDim_);
        if (PrepareDbiTaskForInstrProf(ProfDBIType::OPERAND_RECORD, memSize) && originfunc_ != nullptr) {
            originfunc_(stubFunc_, blockDim_, &newArgsInfo_, smDesc_, stm_, flags_, cfgInfo_);
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

void HijackedFuncOfKernelLaunchWithFlagV2::ProfPreForInstrProf(const std::function<bool(void)> &func,
                                                               const std::function<void(const std::string &)> &bbCountTask,
                                                               rtStream_t stm)
{
    auto funcStub = [this]() {
        return (rtKernelLaunchWithFlagV2Origin(this->stubFunc_, this->blockDim_, this->argsInfo_, this->smDesc_,
                                                this->stm_, this->flags_, this->cfgInfo_) == RT_ERROR_NONE);
    };
    if (ProfConfig::Instance().IsPCSamplingEnabled() && KernelContext::Instance().HasSimtSymbol()) {
        if (PrepareDbiTaskForInstrProf(ProfDBIType::INSTR_PROF_START, INSTR_PROF_MEMSIZE)) {
            KernelContext::StubFuncPtr stubFuncPtr{this->stubFunc_};
            uint64_t kernelAddr;
            if (!KernelContext::Instance().GetDeviceContext().GetKernelAddr(
                KernelContext::StubFuncArgs{stubFuncPtr.value, nullptr}, kernelAddr)) {
                WARN_LOG("Can not get kernel addr for kernel start stub");
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

void HijackedFuncOfKernelLaunchWithFlagV2::Pre(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
    rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags, const rtTaskCfgInfo_t *cfgInfo)
{
    LogRtArgsExt(argsInfo);
    if (!argsInfo) {
        WARN_LOG("argsInfo is null, stop hijackting.");
        return;
    }
    InitParam(stubFunc, blockDim, argsInfo, smDesc, stm, flags, cfgInfo);

    auto bbCountTask = [this](const std::string &outputPath = "") {
        DBITaskConfig::Instance().argsSize_ = GetArgsSize(&newArgsInfo_);
        if (BBCountDumper::Instance().Replace(&stubFunc_, launchId_, outputPath)) {
            memSize_ = BBCountDumper::Instance().GetMemSize(regId_, outputPath);
            memInfo_ = InitMemory(memSize_);
            if (memInfo_ != nullptr) {
                ExpandArgs(&newArgsInfo_, argsVec_, memInfo_, hostInput_, DBITaskConfig::Instance().argsSize_);
            }
        }
    };
    if (IsOpProf()) {
        if (ProfConfig::Instance().IsSimulator()) {
            KernelContext::RegisterEvent registerEvent;
            KernelContext::Instance().GetRegisterEvent(regId_, registerEvent);
            profObj_->ProfInit(registerEvent.hdl, stubFunc);
         } else {
            auto func = [stubFunc, blockDim, argsInfo, smDesc, stm, flags, cfgInfo]() {
                return (rtKernelLaunchWithFlagV2Origin(stubFunc, blockDim, argsInfo, smDesc, stm, flags, cfgInfo) == RT_ERROR_NONE);
            };
            ProfPreForInstrProf(func, bbCountTask, stm);
        }
    }

    if (IsSanitizer()) {
        SanitizerPre();
    }
}

// 调优自定义插桩统一调用此函数 
bool HijackedFuncOfKernelLaunchWithFlagV2::PrepareDbiTaskForInstrProf(ProfDBIType mode, uint64_t memSize)
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
    if (!ExpandArgs(&this->newArgsInfo_, this->argsVec_, memInfo_, hostInput_, DBITaskConfig::Instance().argsSize_) ||
        !RunDBITask(&this->stubFunc_)) {
        ERROR_LOG("Stub run failed, dbi mode is %d", static_cast<uint32_t>(mode));
        return false;
    }
    return true;
}

rtError_t HijackedFuncOfKernelLaunchWithFlagV2::Call(const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
    rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags, const rtTaskCfgInfo_t *cfgInfo)
{
    Pre(stubFunc, blockDim, argsInfo, smDesc, stm, flags, cfgInfo);
    if (this->originfunc_ == nullptr) {
        ERROR_LOG("HijackedFuncOfKernelLaunchWithFlagV2 Hijacked func pointer is nullptr.");
        return RT_ERROR_RESERVED;
    }
    if (IsOpProf() && profObj_ && !profObj_->IsNeedRunOriginLaunch()) {
        return Post(RT_ERROR_NONE);
    }
    return Post(this->originfunc_(this->stubFunc_, blockDim, &this->newArgsInfo_, smDesc, stm, flags, cfgInfo));
}

void HijackedFuncOfKernelLaunchWithFlagV2::SanitizerPost()
{
    if ((this->memInfo_ || isSink_) && !this->skipSanitizer_) {
        // wait for kernel execution done, and catch potential exception
        rtStreamSynchronizeOrigin(this->stm_);
        if (isSink_) {
            KernelDumper::Instance().LaunchDumpTask(stm_);
            return;
        }

        KernelContext::LaunchEvent event;
        KernelContext::Instance().GetLaunchEvent(launchId_, event);

        ReportOpMallocInfo(&this->newArgsInfo_, KernelContext::Instance().GetOpMemInfo());
        __sanitizer_finalize(this->memInfo_, this->blockDim_);
        ReportSectionsFree(event.pcStartAddr, sections_);
        ReportOverflowFree(KernelContext::Instance().GetOpMemInfo());
        ReportOpFreeInfo(KernelContext::Instance().GetOpMemInfo());
    }
}

rtError_t HijackedFuncOfKernelLaunchWithFlagV2::Post(rtError_t ret)
{
    if (!this->argsInfo_) {
        return ret;
    }

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
