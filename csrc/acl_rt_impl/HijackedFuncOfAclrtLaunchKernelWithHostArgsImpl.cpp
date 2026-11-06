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
#include "runtime/RuntimeOrigin.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "runtime/inject_helpers/MemoryContext.h"
#include "runtime/inject_helpers/LaunchManager.h"
#include "runtime/inject_helpers/ArgsRawContext.h"
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

#include <algorithm>

HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl::HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtLaunchKernelWithHostArgsImpl") {}

static void ReportKernelBinary(const RegisterContextSP& regCtx)
{
    auto const &elfData = regCtx->GetElfData();
    int32_t deviceId = 0;
    aclrtGetDeviceImplOrigin(&deviceId);
    LocalDevice::GetInstance(deviceId).NotifyKernelBinary(elfData);
}

// 读取 kernel 自己声明的参数区大小（含编译器为 sanitizer 追加的隐式记录区参数槽）。
// headers 由调用方传入已解析好的 section 表，避免同一次 launch 里重复解析 ELF。
static bool GetKernelDeclaredArgSize(const std::vector<char> &elfData,
    const std::map<std::string, Elf64_Shdr> &headers, uint32_t &kernelArgSize)
{
    auto iter = headers.find("__CCE_KernelArgSize");
    if (iter == headers.end() || iter->second.sh_size < sizeof(uint32_t)) {
        return false;
    }
    if (iter->second.sh_offset > elfData.size() ||
        elfData.size() - iter->second.sh_offset < sizeof(uint32_t)) {
        return false;
    }
    // 按字节拷贝：目标也转成 char*，否则 std::copy_n 会按 uint32_t 元素写 4 个元素，越界
    std::copy_n(elfData.data() + iter->second.sh_offset, sizeof(uint32_t),
        reinterpret_cast<char *>(&kernelArgSize));
    return true;
}

// 判断参数偏移是否落在 placeholder 指向的内嵌数据区内。
// placeholder.dataOffset 是这段数据在 hostArgs 缓冲区中的起始位置，argsSize 覆盖了数据本身。
// 因此 dataOffset <= offset 时该位置是数据，而不是编译器为记录区指针真正预留的空槽。
static bool IsSlotInPlaceholderData(const std::vector<aclrtPlaceHolderInfo> &placeHolders, size_t offset)
{
    for (const auto &placeholder : placeHolders) {
        if (placeholder.dataOffset <= offset) {
            return true;
        }
    }
    return false;
}

// 计算静态插桩 kernel 隐式记录区参数在参数区里的偏移。
// 静态插桩时（bishengir 的 AppendSanitizerData pass）编译器在参数区末尾追加了一个 GM 指针参数，
// 记录区指针必须写到该参数的真实偏移上。先查 runtime 参数表（.ascend.meta 登记的 offset），
// 查不到时（如 triton 的裸 .ascend.meta）用 __CCE_KernelArgSize 取末尾 8 字节。
// 两种定位的偏移都可能落在 placeholder 内嵌数据区（声明区末尾被数据覆盖，槽位并未真正预留），
// 这类偏移不能覆盖，返回 false 由调用方改为显式插入。两者都不满足也返回 false，退回尾部追加。
static bool LocateRecordArgSlot(size_t argsSize, uint32_t kernelArgSize, void *funcHandle,
    const std::vector<aclrtPlaceHolderInfo> &placeHolders, size_t &offset)
{
    size_t paramCount = 0;
    size_t paramOffset = 0;
    size_t paramSize = 0;
    // 最后一个参数的结束位置必须正好等于声明区大小，否则不确定它是不是末尾那个隐式参数，
    // 不能覆盖，避免写坏真实入参。
    if (funcHandle != nullptr && kernelArgSize != 0 &&
        aclrtFunctionGetParamCountImplOrigin(funcHandle, &paramCount) == ACL_SUCCESS && paramCount > 0 &&
        aclrtFunctionGetParamInfoImplOrigin(funcHandle, paramCount - 1, &paramOffset, &paramSize) == ACL_SUCCESS &&
        paramSize == sizeof(uintptr_t) && paramOffset + paramSize == kernelArgSize &&
        paramOffset + paramSize <= argsSize &&
        !IsSlotInPlaceholderData(placeHolders, paramOffset)) {
        offset = paramOffset;
        return true;
    }
    if (kernelArgSize >= sizeof(uintptr_t) && (kernelArgSize % sizeof(uintptr_t)) == 0 &&
        argsSize >= kernelArgSize &&
        !IsSlotInPlaceholderData(placeHolders, kernelArgSize - sizeof(uintptr_t))) {
        offset = kernelArgSize - sizeof(uintptr_t);
        return true;
    }
    return false;
}

bool HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl::InitParam(
    aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream, aclrtLaunchKernelCfg *cfg,
    void *hostArgs, size_t argsSize, aclrtPlaceHolderInfo *placeHolderArray, size_t placeHolderNum)
{
    refreshParamFunc_ = [this, funcHandle, blockDim, stream, cfg,
            hostArgs, argsSize, placeHolderArray, placeHolderNum]() {
        funcHandle_ = funcHandle;
        blockDim_ = blockDim;
        argsSize_ = argsSize;
        stream_ = stream;
        hostArgs_ = hostArgs;
        placeHolderNum_ = placeHolderNum;
        devId_ = DeviceContext::GetRunningDeviceId();
        if (placeHolderArray) {
            placeHolderArray_.resize(placeHolderNum);
            for (size_t i = 0; i < placeHolderNum; i++) {
                placeHolderArray_[i].addrOffset = placeHolderArray[i].addrOffset;
                placeHolderArray_[i].dataOffset = placeHolderArray[i].dataOffset;
            }
        }
        cfg_ = cfg;
        skipSanitizer_ = false;
        abortLaunch_ = false;
    };
    refreshParamFunc_();
    auto funcCtx = FuncManager::Instance().GetContext(funcHandle);
    if (funcCtx == nullptr) {
        funcCtx = CreateFuncContext(funcHandle);
    }
    if (funcCtx && funcCtx->GetRegisterContext()->GetMagic() == RT_DEV_BINARY_MAGIC_ELF_AICPU) {
        return false;
    }
    argsCtx_ = ArgsManager::Instance().CreateContext(hostArgs, argsSize, placeHolderArray_);
    launchCtx_ = LaunchManager::Local().CreateContext(funcHandle, blockDim, stream, cfg, argsCtx_);
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

void HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl::ProfPre(const std::function<bool(void)> &func,
    const std::function<void(const std::string &)> &bbCountTask, aclrtStream stm)
{
    profObj_->ProfInit(nullptr, nullptr, false); // pc_start落盘txt文件
    profObj_->ProfData(stm, func);
    if (profObj_->IsBBCountNeedGen() && bbCountTask != nullptr) {
        refreshParamFunc_();
        bbCountTask(ProfDataCollect::GetAicoreOutputPath(devId_));
    }
}

void HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl::SanitizerPre()
{
    // mssanitizer SIGINT 信号处理接管
    BindSigIntHandler();

    std::string kernelName = launchCtx_->GetFuncContext()->GetKernelName();
    skipSanitizer_ = SkipSanitizer(kernelName);
    if (skipSanitizer_) {
        return;
    }
    if (isSink_) { return; }
    ReportKernelSummary(launchCtx_);
    ReportKernelBinary(launchCtx_->GetFuncContext()->GetRegisterContext());

    // 先取原始参数信息并完成动态插桩，保证后续的注册与下发都基于插桩后的 kernel
    auto argsRawCtx = std::static_pointer_cast<ArgsRawContext>(launchCtx_->GetArgsContext());
    hostArgs_ = argsRawCtx->GetArgs();
    argsSize_ = argsRawCtx->GetArgsSize();
    placeHolderArray_ = argsRawCtx->GetPlaceholderInfo();
    placeHolderNum_ = placeHolderArray_.size();
    // bisheng-tune 通过 --tune-argsize 拿 kernel 参数区大小来排记录区参数，必须传本次 launch 的值。
    // 该字段是全局的，不刷新就会沿用其它 launch 留下的值。
    DBITaskConfig::Instance().argsSize_ = argsRawCtx->GetLastParamOffset();
    // 动态插桩会产生新的 kernel 句柄，但只有 memInfo 参数也补齐后才允许切换下发，
    // 否则会出现“插桩 kernel + 缺 memInfo 的参数”的错配，设备侧会读到非法记录区地址。
    // 这里先记下原始句柄，等初始化和补参全部成功后再提交。
    const auto originDbiFuncCtx = launchCtx_->GetDBIFuncCtx();
    const auto originFuncHandle = funcHandle_;
    const auto newFuncCtx = RunDBITask(launchCtx_);
    if (newFuncCtx) {
        // funcCtx_ 需要先切到插桩后的上下文：注册与回收都基于它的 startPC
        funcCtx_ = newFuncCtx;
    }

    std::map<std::string, Elf64_Shdr> headers;
    const bool hasHeaders = funcCtx_ != nullptr && !funcCtx_->isAiCpu &&
        GetSectionHeaders(funcCtx_->GetRegisterContext()->GetElfData(), headers);
    const bool hasStaticStub = funcCtx_ != nullptr && !funcCtx_->isAiCpu &&
        funcCtx_->GetRegisterContext()->HasStaticStub();
    uint32_t kernelArgSize = 0;
    const bool hasKernelArgSize = hasHeaders &&
        GetKernelDeclaredArgSize(funcCtx_->GetRegisterContext()->GetElfData(), headers, kernelArgSize);
    size_t recordArgOffset = 0;
    const bool hasRecordArgSlot = hasHeaders && hostArgs_ != nullptr && hasStaticStub && hasKernelArgSize &&
        LocateRecordArgSlot(argsSize_, kernelArgSize, funcHandle_, placeHolderArray_, recordArgOffset);
    // 静态插桩 kernel 固定从声明参数区末尾（kernelArgSize - 8）读记录区指针。该槽位被 placeholder
    // 内嵌数据覆盖时（声明区被数据挤占，槽位并未真正预留），不能覆盖数据，改为在 kernel 读取的
    // 偏移处插入 8 字节并后移数据。
    const bool recordSlotInPlaceholderData = !hasRecordArgSlot && hostArgs_ != nullptr && hasStaticStub &&
        hasKernelArgSize && kernelArgSize >= sizeof(uintptr_t) &&
        (kernelArgSize % sizeof(uintptr_t)) == 0 && kernelArgSize <= argsSize_ &&
        IsSlotInPlaceholderData(placeHolderArray_, kernelArgSize - sizeof(uintptr_t));
    if (recordSlotInPlaceholderData) {
        recordArgOffset = kernelArgSize - sizeof(uintptr_t);
    }
    // 槽位由编译器（AppendSanitizerData）追加，kernel 固定从该偏移读 memInfo。
    // 调用方缓冲区此处可能非 0（aclnn 为 tiling 缓冲地址，TRITON_DEVICE_PRINT=1 为 DTData 指针），
    // 仍需覆盖为记录区指针，否则 kernel 会按残留值访问记录区。
    if (hasRecordArgSlot) {
        auto *slotPtr = reinterpret_cast<uint64_t *>(static_cast<uint8_t *>(hostArgs_) + recordArgOffset);
        if (*slotPtr != 0) {
            WARN_LOG("Record arg slot of kernel %s is not empty: offset=%zu argsSize=%zu kernelArgSize=%u "
                "current value 0x%lx. It is declared by the compiler for the sanitizer record area and "
                "will be overwritten with the sanitizer memInfo. If TRITON_DEVICE_PRINT=1 is enabled, "
                "device print and sanitizer share this slot and cannot be used together.",
                kernelName.c_str(), recordArgOffset, argsSize_, kernelArgSize, *slotPtr);
        }
    }

    // section 列表在 Pre 阶段解析，内存上报统一放到 SanitizerPost：
    // tiling 指针由 runtime 在下发时才写入 hostArgs，下发前上报只能拿到 0。
    if (hasHeaders) {
        sections_ = GetAllocSectionHeaders(headers);
    }

    // 预处理失败时不能直接返回：外层 Call 仍会下发，而 funcCtx_ 可能已经指向插桩后的 kernel。
    auto handlePreFailure = [&](const char *reason) {
        if (memInfo_ != nullptr) {
            // 与 SanitizerPost 的回收顺序保持一致；置空后 Post 不会重复回收
            __sanitizer_finalize(memInfo_, blockDim_);
            memInfo_ = nullptr;
        }
        if (hasStaticStub) {
            // 静态插桩 kernel 没有未插桩版本可退，缺 memInfo 下发必然读到非法地址
            ERROR_LOG("Abort launch for kernel %s: %s. The static instrumented kernel requires "
                "sanitizer record args.", kernelName.c_str(), reason);
            abortLaunch_ = true;
            return;
        }
        if (newFuncCtx != nullptr) {
            // 动态插桩失败可回滚到原始 kernel（原始 kernel 不读记录区），本次不检测但功能正常
            WARN_LOG("Rollback to original kernel for %s: %s.", kernelName.c_str(), reason);
            funcHandle_ = originFuncHandle;
            launchCtx_->SetDBIFuncCtx(originDbiFuncCtx);
        }
    };

    memInfo_ = __sanitizer_init(blockDim_);
    if (memInfo_ == nullptr) {
        handlePreFailure("sanitizer init failed");
        return;
    }
    // 写入静态插桩 kernel 的隐式记录区参数槽；没有该槽时退回“尾部追加”逻辑
    if (hasRecordArgSlot) {
        // hostArgs_ 是 ArgsRawContext 里的副本（构造时整块拷贝调用方 buffer），写它不影响调用方内存
        auto *slot = reinterpret_cast<uint64_t *>(static_cast<uint8_t *>(hostArgs_) + recordArgOffset);
        *slot = reinterpret_cast<uint64_t>(memInfo_);
        DEBUG_LOG("Fill sanitizer record arg: offset=%zu argsSize=%zu kernelArgSize=%u placeHolderNum=%zu",
            recordArgOffset, argsSize_, kernelArgSize, placeHolderNum_);
    } else {
        // expand args（必须在 __sanitizer_init 之后，因为需要 memInfo_ 指针）
        bool expanded = false;
        if (recordSlotInPlaceholderData) {
            expanded = argsRawCtx->ExpandArgsAt(&memInfo_, sizeof(uintptr_t),
                static_cast<uint32_t>(recordArgOffset), DBITaskConfig::Instance().argsSize_);
        } else {
            expanded = launchCtx_->GetArgsContext()->ExpandArgs(&memInfo_, sizeof(uintptr_t),
                DBITaskConfig::Instance().argsSize_);
        }
        if (!expanded) {
            handlePreFailure("expand sanitizer kernel args failed");
            return;
        }
        argsRawCtx = std::static_pointer_cast<ArgsRawContext>(launchCtx_->GetArgsContext());
        hostArgs_ = argsRawCtx->GetArgs();
        argsSize_ = argsRawCtx->GetArgsSize();
        placeHolderArray_ = argsRawCtx->GetPlaceholderInfo();
        placeHolderNum_ = placeHolderArray_.size();
        if (recordSlotInPlaceholderData) {
            DEBUG_LOG("Insert sanitizer record arg at declared slot: offset=%zu argsSize=%zu kernelArgSize=%u",
                recordArgOffset, argsSize_, kernelArgSize);
        }
    }
    // memInfo 参数已就位，此时才把动态插桩后的句柄提交给下发
    if (newFuncCtx != nullptr) {
        launchCtx_->SetDBIFuncCtx(newFuncCtx);
        funcHandle_ = newFuncCtx->GetFuncHandle();
    }
}

// 调优自定义插桩统一调用此函数
bool HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl::PrepareDbiTask(ProfDBIType mode, uint64_t memSize) {
    // 每次调用插桩前需要清理插桩用到的成员变量，保证不被上次插桩污染
    refreshParamFunc_();
    KernelMatcher::Config matchConfig;
    std::string path = GetEnv(DEVICE_PROF_DUMP_PATH_ENV);
    std::string pluginPath = ProfConfig::Instance().GetPluginPath(mode);
    std::vector<std::string> extraArgs;
    std::string tuneLogPath;
    DbiRecordTaskHelper::AppendExtraInfo(mode, ProfDataCollect::GetAicoreOutputPath(devId_), tuneLogPath, extraArgs);
    DBITaskConfig::Instance().Init(BIType::CUSTOMIZE, pluginPath, matchConfig, path, tuneLogPath, extraArgs);
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
        placeHolderArray_ = argsRawCtx->GetPlaceholderInfo();
        hostArgs_ = argsRawCtx->GetArgs();
        argsSize_ = argsRawCtx->GetArgsSize();
        return true;
    }
    WARN_LOG("New function context get failed, dbi mode is %d", static_cast<uint32_t>(mode));
    return false;
}

void HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl::ProfPreForInstrProf(const std::function<bool(void)> &func,
    const std::function<void(const std::string &)> &bbCountTask, rtStream_t stream)
{
    auto funcStub = [this]() {
        return (aclrtLaunchKernelWithHostArgsImplOrigin(funcHandle_, blockDim_, stream_, cfg_, hostArgs_, argsSize_,
                                                        placeHolderArray_.data(), placeHolderArray_.size()) == ACL_SUCCESS);
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
    ProfPre(func, bbCountTask, stream);
}

void HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl::Pre(aclrtFuncHandle funcHandle, uint32_t blockDim,
    aclrtStream stream, aclrtLaunchKernelCfg *cfg, void *hostArgs, size_t argsSize,
    aclrtPlaceHolderInfo *placeHolderArray, size_t placeHolderNum)
{
    if (!InitParam(funcHandle, blockDim, stream, cfg,
                   hostArgs, argsSize, placeHolderArray, placeHolderNum)) {
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
            hostArgs_ = argsRawCtx->GetArgs();
            argsSize_ = argsRawCtx->GetArgsSize();
            placeHolderArray_ = argsRawCtx->GetPlaceholderInfo();
        }
    };
    if (IsOpProf() && profObj_) {
        if (ProfConfig::Instance().IsSimulator()) {
            profObj_->ProfInit(nullptr, nullptr, false); // 完全切换至aclrt接口时需要删除该函数入参
        } else {
            auto func = [funcHandle, blockDim, stream, cfg, hostArgs, argsSize, placeHolderArray, placeHolderNum]() {
                return (aclrtLaunchKernelWithHostArgsImplOrigin(funcHandle, blockDim, stream, cfg, hostArgs, argsSize,
                                                                placeHolderArray, placeHolderNum) == ACL_SUCCESS);
            };
            ProfPreForInstrProf(func, bbCountTask, stream);
        }
    }
    if (IsSanitizer()) {
        this->SanitizerPre();
    }
}
aclError HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl::Call(aclrtFuncHandle funcHandle, uint32_t blockDim,
    aclrtStream stream, aclrtLaunchKernelCfg *cfg, void *hostArgs, size_t argsSize,
    aclrtPlaceHolderInfo *placeHolderArray, size_t placeHolderNum)
{
    Pre(funcHandle, blockDim, stream, cfg,
        hostArgs, argsSize, placeHolderArray, placeHolderNum);
    if (originfunc_ == nullptr) {
        ERROR_LOG("%s Hijacked func pointer is nullptr.", __FUNCTION__);
        return EmptyFunc();
    }
    if (abortLaunch_) {
        // 插桩后的参数无法补齐且无可回滚的原始 kernel，继续下发会让检测代码读到非法记录区地址
        return ACL_ERROR_INTERNAL_ERROR;
    }
    if (IsOpProf() && profObj_ && !profObj_->IsNeedRunOriginLaunch()) {
        return Post(ACL_ERROR_NONE);
    }
    return Post(originfunc_(funcHandle_, blockDim, stream, cfg,
                            hostArgs_, argsSize_, placeHolderArray_.data(), placeHolderNum));
}

void HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl::SanitizerPost()
{
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

        // 内存上报放在下发之后：tiling 指针此时已由 runtime 写入 hostArgs，
        // 上报拿到的是真实设备地址；记录仍归属本次 launch，回放时排在访问记录之前。
        if (!funcCtx_->isAiCpu && !sections_.empty()) {
            ReportSectionsMalloc(funcCtx_->GetStartPC(), sections_);
        }
        AclrtLaunchArgsInfo launchInfo{};
        launchInfo.hostArgs = hostArgs_;
        launchInfo.argsSize = argsSize_;
        launchInfo.placeHolderArray = reinterpret_cast<aclrtPlaceHolderInfo *>(placeHolderArray_.data());
        launchInfo.placeHolderNum = placeHolderNum_;
        ReportOpMallocInfo(launchInfo, LaunchManager::Local().GetCurrentMemInfo());

        if (!funcCtx_->isAiCpu) {
            auto startPC = funcCtx_->GetStartPC();
            __sanitizer_finalize(memInfo_, blockDim_);
            ReportSectionsFree(startPC, sections_);
        } else {
            __sanitizer_finalize(memInfo_, blockDim_);
        }

        ReportOpFreeInfo(LaunchManager::Local().GetCurrentMemInfo());
        ExitAfterProcess();
    }
}

void HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl::RunDbiRecordTask(ProfDBIType mode, const char *failedLog)
{
    if (!DbiRecordTaskHelper::IsNeedGen(profObj_.get(), mode)) {
        return;
    }
    aclrtSynchronizeStreamImplOrigin(stream_);
    // Call 已消耗过一次算子输入；每次 DBI 重放前恢复输入到快照态，
    // 否则后续插桩读到的是被污染的数据（app 模式 Call 跳过 origin，输入未被消耗，无需恢复）
    if (!ProfConfig::Instance().IsAppReplay() && !MemoryContext::Instance().Restore()) {
        WARN_LOG("Restore input data before DBI record task failed, mode=%u", static_cast<uint32_t>(mode));
    }
    uint64_t memSize = DbiRecordTaskHelper::GetDbiRecordMemSize(mode, blockDim_, MAX_BLOCK);
    if (!PrepareDbiTask(mode, memSize) || originfunc_ == nullptr) {
        return;
    }
    originfunc_(funcHandle_, blockDim_, stream_, cfg_, hostArgs_, argsSize_, placeHolderArray_.data(),
        placeHolderArray_.size());
    aclError ret = aclrtSynchronizeStreamImplOrigin(stream_);
    if (ret == ACL_SUCCESS) {
        DbiRecordTaskHelper::CollectData(profObj_.get(), mode, memSize_, memInfo_);
        return;
    }
    WARN_LOG("%s", failedLog);
}

void HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl::ProfPost()
{
    if (profObj_->IsBBCountNeedGen()) {
        aclrtSynchronizeStreamImplOrigin(stream_);
        profObj_->GenBBcountFile(regId_, memSize_, memInfo_);
    }
    for (const auto &task : DbiRecordTaskHelper::DBI_RECORD_TASKS) {
        RunDbiRecordTask(task.mode, task.aclFailedLog);
    }
    profObj_->PostProcess();
}

aclError HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl::Post(aclError ret)
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
