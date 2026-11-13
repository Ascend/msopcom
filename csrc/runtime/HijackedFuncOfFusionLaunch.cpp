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

#include "HijackedFunc.h"
#include "RuntimeConfig.h"
#include "runtime/inject_helpers/ArgsManager.h"

#include "core/FuncSelector.h"
#include "runtime/inject_helpers/DBITask.h"
#include "runtime/inject_helpers/DevMemManager.h"
#include "runtime/inject_helpers/FusionLaunchArgs.h"
#include "runtime/inject_helpers/InstrReport.h"
#include "runtime/inject_helpers/LaunchManager.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/MemGuard.h"
#include "runtime/inject_helpers/SyncStreamWithInterrupt.h"

namespace {
// 参数视图指向本次 fusion launch 的局部副本；离开前清空参数元数据并恢复临时修改的三个标量字段。
struct FusionContextCleanup {
    KernelContext &context = KernelContext::Instance();
    uint32_t argsSize = context.GetArgsSize();
    uint32_t paramNum = context.GetKernelParamNum();
    uint32_t simtUb = context.GetSimtUbDynamicSize();
    bool active = true;
    // 清理操作可重复调用；显式清理后，析构兜底不会再次修改上下文。
    void Clean()
    {
        if (!active) { return; }
        active = false;
        context.ClearArgsInfo();
        context.SetArgsSize(argsSize);
        context.SetKernelParamNum(paramNum);
        context.SetSimtUbDynamicSize(simtUb);
    }
    ~FusionContextCleanup() { Clean(); }
};

// 配对撤销本次 ELF 段、溢出区和算子内存的上报；实际 device 记录区由 DevMemManager 释放。
struct FusionMemoryCleanup {
    FusionMemoryCleanup(uint64_t address, const std::vector<Elf64_Shdr> &headers, KernelContext::OpMemInfo &memInfo)
        : pc(address), sections(headers), info(memInfo) {}
    uint64_t pc;
    const std::vector<Elf64_Shdr> &sections;
    KernelContext::OpMemInfo &info;
    bool active = true;
    // 只执行一次 FREE 上报；支持退出前显式调用，并由析构覆盖其他离开路径。
    void Clean()
    {
        if (!active) { return; }
        active = false;
        ReportSectionsFree(pc, sections);
        ReportOverflowFree(info);
        ReportOpFreeInfo(info);
    }
    ~FusionMemoryCleanup() { Clean(); }
};

// 按对齐后的原参数头长度插桩 AIC，成功时更新副本中的 AIC 句柄，返回 DBI 处理结果。
// 新增记录指针及内联数据不计入该长度；临时 DBI 配置在正常返回或异常展开时恢复。
bool InstrumentFusion(FusionLaunchArgs &launch)
{
    auto &dbi = DBITaskConfig::Instance(); // thread_local，与其他下发线程隔离
    struct ArgsSizeRestore {
        uint32_t &size;
        uint32_t saved;
        ~ArgsSizeRestore() { size = saved; }
    } restore{dbi.argsSize_, dbi.argsSize_};
    dbi.argsSize_ = launch.RecordOffset();
    return RunDBITask(&launch.Aicore().hdl, launch.Aicore().tilingKey);
}

// 调用原始 rtFusionLaunch 后，同步流、检查保护区并处理检测记录，最后释放 device 记录区。
// 即使 rtFusionLaunch 或流同步返回错误，也沿用其他 launch hook 的后处理流程。
void ProcessFusionRecords(rtStream_t stream, FusionLaunchArgs &launch,
    KernelContext::OpMemInfo &info, uint8_t *record)
{
    SyncStreamWithInterrupt(stream);
    MemoryGuard::Instance().CheckAllMemGuard();
    // 与其他 launch hook 一致：提交/同步报错也处理已有记录，并完成 finalize 状态清理。
    ReportOpMallocInfo(launch.KernelArgs(), info);
    __sanitizer_finalize(record, launch.BlockDim());
    DevMemManager::Instance().Free();
}
} // namespace

HijackedFuncOfFusionLaunch::HijackedFuncOfFusionLaunch() : HijackedFuncType(RuntimeLibName(), "rtFusionLaunch")
{
}

rtError_t HijackedFuncOfFusionLaunch::Call(void *fusionInfo, rtStream_t stm, rtFusionArgsEx_t *argsInfo)
{
    if (!originfunc_) {
        return EmptyFunc();
    }
    // 不支持适配或准备失败时，先清理已建立的临时状态，再用调用者的原始参数转发并返回原始结果。
    auto forward = [&]() { return OriginCall(fusionInfo, stm, argsInfo); };
    // 仅支持检测和 A5；binded = true 表示stream绑定model或处于capture状态，不在检测插桩范围内，直接放行
    if (!IsSanitizer() || !DeviceContext::Local().IsAscend950() || LaunchManager::GetOrCreateStreamInfo(stm).binded) {
        return forward();
    }
    FusionLaunchArgs launch;
    if (!launch.Init(fusionInfo, argsInfo)) {
        WARN_LOG("Unsupported fusion launch arguments; skipping dynamic instrumentation.");
        return forward();
    }
    auto &context = KernelContext::Instance();
    auto &aic = launch.Aicore();
    rtDevBinary_t binary{};
    // 按 AIC 原始 hdl 获取注册时保存的 binary；无法取得 binary 或已有静态桩时原样转发，不重复进行动态插桩。
    if (!context.GetDevBinary(KernelContext::KernelHandlePtr{aic.hdl}, binary) || HasStaticStub(binary)) {
        WARN_LOG("Fusion AIC binary unavailable for dynamic instrumentation.");
        return forward();
    }
    // 修改上下文前保存标量字段；回退调用原始函数前也必须清理局部参数视图。
    FusionContextCleanup contextCleanup;
    auto forwardWithContext = [&]() {
        contextCleanup.Clean();
        return forward();
    };
    // 登记 kernel 身份和本次运行信息，建立 launch 上下文。
    context.AddLaunchEvent(aic.hdl, aic.tilingKey, launch.BlockDim(), launch.KernelArgs(), stm);
    const auto launchId = context.GetLaunchId();
    /*
    原始总长度 S：调用者参数缓冲区的全部字节数，可能包括内联数据。不是新增记录指针的位置，不能直接拿来设置这里的参数头长度
    HeadSize() H：内联数据之前的原参数头长度；无内联描述时为原始总长度。用于确定需要保留的原头部范围
    RecordOffset() R：H 按八字节向上对齐后的位置。既是对齐后原参数区的长度，也是随后插入记录指针的字节偏移
    例如，原始参数总长 S=64 字节，最早的内联数据位于偏移 33，则 H=33、R=40。argsSize=40、kernelParamNum=5。在AppendRecord()扩展完成后，布局为：
    字节区间       内容
    [0, 33)        原参数头，内容及各地址槽偏移保持不变
    [33, 40)       为记录指针对齐补齐的 7 字节
    [40, 48)       工具新增的记录缓冲区地址，长度 8 字节
    [48, 79)       搬移后的原内联数据
    */
    context.SetArgsSize(launch.RecordOffset()); // 把对齐后的原参数头长度写入工具上下文
    // SetKernelParamNum(R/8) 是工具使用的参数槽数量，不等于 tensor 个数，头部还可能包含工作区、预留槽及对齐补齐。
    // 它不包含新增记录指针自身，也不把后面的内联数据计入其中。
    context.SetKernelParamNum(launch.RecordOffset() / sizeof(uint64_t));
    // fusion场景的args不是rtArgsEx_t，传nullptr，tiling由SetTiling处理
    context.ParseMetaDataFromBinary(binary, nullptr);
    auto &info = context.GetOpMemInfo();
    if (!launch.SetTiling(info.tilingParamsNo)) {
        return forwardWithContext();
    }
    const auto &kernelArgs = *launch.KernelArgs();
    if (kernelArgs.hasTiling != 0 && info.tilingDataSize != 0) {
        if (info.tilingDataSize > kernelArgs.argsSize - kernelArgs.tilingDataOffset) {
            return forwardWithContext();
        }
        const auto *tiling = static_cast<const uint8_t *>(kernelArgs.args) + kernelArgs.tilingDataOffset;
        // 获取动态输入的实际长度
        UpdateInputSizeWithTilingData(std::vector<uint8_t>(tiling, tiling + info.tilingDataSize), info);
    }
    // 跳过的槽数与输入参数槽总数不能超出原参数头，避免读取内联区；跳过当前 kernel 时也原样转发。
    const uint64_t slots = launch.HeadSize() / sizeof(uint64_t);
    if (info.skipNum > slots || info.inputParamsAddrInfos.size() > slots - info.skipNum ||
        SkipSanitizer(context.GetLaunchName())) {
        return forwardWithContext();
    }
    context.ArchiveMemInfo();
    // 后续上报和清理使用本次元数据快照，不依赖 DBI 流程之后实时上下文中保留的元数据。
    auto launchInfo = info;
    // 在 DBI 前绑定 SIGINT 处理；插桩时临时设置 DBI 参数长度，由 InstrumentFusion 恢复。
    BindSigIntHandler();
    const bool instrumented = InstrumentFusion(launch);
    if (!instrumented) {
        return forwardWithContext();
    }

    // 获取插桩后的 binary 和 ELF 段；下方上报的内存由 memoryCleanup 配对清理。
    KernelContext::LaunchEvent event{};
    if (!context.GetLaunchEvent(launchId, event)) {
        return forwardWithContext();
    }
    rtDevBinary_t instrumentedBinary{};
    std::map<std::string, Elf64_Shdr> headers;
    if (!context.GetDevBinary(KernelContext::KernelHandlePtr{event.hdl}, instrumentedBinary, true) ||
        !GetSectionHeaders(instrumentedBinary, headers)) {
        return forwardWithContext();
    }
    auto sections = GetAllocSectionHeaders(headers);
    ReportSectionsMalloc(event.pcStartAddr, sections);
    ReportOverflowMalloc(launchInfo);
    FusionMemoryCleanup memoryCleanup{event.pcStartAddr, sections, launchInfo};
    // 沿用既有输入内存缓存流程；零长度占位不表示允许访问地址零，也不扩大合法范围。
    MemoryManage::Instance().CacheMemoryCount(launchInfo.inputParamsAddrInfos.size() + 1);
    if (!launchInfo.inputParamsAddrInfos.empty()) {
        MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(
            0, launchInfo.inputParamsAddrInfos.front().memInfoSrc, 0, false);
    }
    // 使用本次动态 UB 大小初始化 device 记录区。
    auto &memory = DevMemManager::Instance();
    memory.SetSkipKernelFlag(false);
    context.SetSimtUbDynamicSize(launch.DynUbufSize()); // 避免沿用前次 launch 的值
    uint8_t *record = __sanitizer_init(launch.BlockDim());
    if (!record || !launch.AppendRecord(record)) {
        // 回退前复位初始化标志，释放记录区，并配对清理已上报的内存和临时上下文。
        memory.SetMemoryInitFlag(false);
        memory.Free();
        memoryCleanup.Clean();
        return forwardWithContext();
    }
    // 提交前填充 MemoryGuard 并上报 kernel 信息，供执行后的检查和报告使用。
    MemoryGuard::Instance().FillAllMemGuard();
    ReportKernelSummary(launchId);
    context.ReportKernelBinary(KernelContext::KernelHandlePtr{event.hdl});
    // 保留 CCU 描述、子任务顺序、tilingKey、全部 launch 属性及原 stream，只有AIC的hdl是插桩后的。
    const rtError_t ret = OriginCall(launch.Tasks(), stm, launch.Args());
    ProcessFusionRecords(stm, launch, launchInfo, record);
    // ExitAfterProcess 可能触发 SIGINT 退出，必须在此前显式清理，不能仅依赖栈对象析构。
    memoryCleanup.Clean();
    contextCleanup.Clean();
    ExitAfterProcess();
    return ret;
}
