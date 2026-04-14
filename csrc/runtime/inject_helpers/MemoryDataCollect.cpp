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


#include "MemoryDataCollect.h"
#include <cinttypes>
#include <elf.h>
#include <map>
#include <unordered_map>
#include <vector>

#include "KernelContext.h"
#include "ascend_hal/AscendHalOrigin.h"
#include "utils/InjectLogger.h"
#include "runtime.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "utils/ElfLoader.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"
#include "ArgsManager.h"

namespace {

/// 函数返回值表示是否缓存当前传入的addr
bool ProcessMc2CtxAddr(uint32_t index, uint64_t addr,
    std::function<void(const KernelContext::AddrInfo &addrInfo)> func)
{
    auto &context = KernelContext::Instance();
    // 如果当前算子不是mc2算子或者是mc2但index位置不是mc2_context，则缓存当前addr
    if (!context.GetMc2CtxFlag() || index != MC2_CONTEXT_PARAMS_INDEX) {
        return true;
    }
    // 当前index为mc2_context，则解析共享内存地址；
    std::vector<KernelContext::AddrInfo> mc2CtxAddrs = context.ParseMc2CtxAddrs(addr);
    for (auto const &addrInfo : mc2CtxAddrs) {
        func(addrInfo);
    }
    /// 如果当前mc2算子经过adump接口，则缓存当前addr
    if (ArgsManager::Instance().GetThroughAdumpFlag()) {
        return true;
    }
    return false;
}

bool IsSameMemInfoSrc(MemInfoSrc lhs, MemInfoSrc rhs)
{
    /// acl和rt被认为是相同的内存来源
    if ((lhs == MemInfoSrc::ACL || lhs == MemInfoSrc::RT) && (rhs == MemInfoSrc::ACL || rhs == MemInfoSrc::RT)) {
        return true;
    }
    return lhs == rhs;
}

} // namespace [Dummy]

inline bool IsAddrInRange(uint64_t addr, uint64_t size, uint64_t thresholdAddr, uint64_t thresholdSize)
{
    if (addr >= thresholdAddr && thresholdSize >= size && addr + size <= thresholdAddr + thresholdSize) {
        return true;
    }
    return false;
}

inline bool hasExtra(std::set<KernelContext::AddrInfo> &addrsVec)
{
    for (const auto &addrs : addrsVec) {
        if (addrs.memInfoSrc == MemInfoSrc::EXTRA) {
            return true;
        }
    }
    return false;
}

template <>
void MemoryManage::CacheMemory<MemoryOpType::MALLOC>(uint64_t addr, MemInfoSrc infoSrc,
    uint64_t size, bool isUpdateCount)
{
    if (isUpdateCount) {
        mallocCount_++;
    }
    this->memoryOpAddrInfos_.insert({addr, size, infoSrc});
}

template <>
void MemoryManage::CacheMemory<MemoryOpType::FREE>(uint64_t addr, MemInfoSrc infoSrc,
    uint64_t size, bool isUpdateCount)
{
    (void)size;
    (void)isUpdateCount;
    for (const auto &it : this->memoryOpAddrInfos_) {
        if (it.addr == addr) {
            if (IsSameMemInfoSrc(it.memInfoSrc, infoSrc)) {
                this->memoryOpAddrInfos_.erase(it);
                mallocCount_--;
                return;
            }
        }
    }
    ERROR_LOG("illegal free addr:%lx infoSrc:%u", addr, static_cast<uint32_t>(infoSrc));
}

void MemoryManage::ProcessMstxHeapMem(AddrsSet &cacheRtAclMem, const KernelContext::AddrInfo &addrInfo) const
{
    for (auto it = cacheRtAclMem.begin(); it != cacheRtAclMem.end();) {
        /// 如果在范围内，则rtMalloc的范围和mstx的差集即为当前rtMalloc的合法范围
        if (IsAddrInRange(addrInfo.addr, addrInfo.length, it->addr, it->size)) {
            if (addrInfo.addr > it->addr) {
                cacheRtAclMem.insert({it->addr, addrInfo.addr - it->addr});
            }
            uint64_t rightSize = it->addr - addrInfo.addr + it->size - addrInfo.length;
            if (rightSize > 0) {
                cacheRtAclMem.insert({addrInfo.addr + addrInfo.length, rightSize});
            }
            cacheRtAclMem.erase(it);
            return;
        } else {
            ++it;
        }
    }
}

MemoryManage::AddrsVec MemoryManage::ExtractValidMems(std::set<KernelContext::AddrInfo> &addrInfos) const
{
    bool isExtra = hasExtra(addrInfos);
    AddrsVec ranges;
    AddrsSet cacheRtAclMems;
    for (const auto &addrs : addrInfos) {
        if (addrs.length == 0U) {
            continue;
        }
        MemInfoSrc infoSrc = addrs.memInfoSrc;
        if (isExtra && (infoSrc == MemInfoSrc::EXTRA || infoSrc == MemInfoSrc::MANUAL ||
            infoSrc == MemInfoSrc::BYPASS)) {
            ranges.push_back({addrs.addr, addrs.length});
            continue;
        }
        if (isExtra) { continue; }

        if (infoSrc == MemInfoSrc::ACL || infoSrc == MemInfoSrc::RT) {
            cacheRtAclMems.insert({addrs.addr, addrs.length});
        } else if (infoSrc == MemInfoSrc::MSTX_HEAP) {
            ProcessMstxHeapMem(cacheRtAclMems, addrs);
        } else {
            ranges.push_back({addrs.addr, addrs.length});
        }
    }
    for (const auto &it : cacheRtAclMems) {
        ranges.push_back(it);
    }
    return ranges;
}

MemoryManage::AddrsVec MemoryManage::MergeHostMems()
{
    std::lock_guard<std::mutex> lock(memoryMutex_);
    if (memoryOpAddrInfos_.empty() || mallocCount_ == 0) {
        return {};
    }

    AddrsVec ranges = ExtractValidMems(memoryOpAddrInfos_);
    // 1. 排序区间（按起始地址升序）
    std::sort(ranges.begin(), ranges.end(), [](const HostMemoryInfo &a, const HostMemoryInfo &b) {
        return a.addr < b.addr; // 按起始地址排序
    });

    // 2. 合并区间
    AddrsVec merged;
    if (ranges.size() > 0) {
        merged.push_back(ranges[0]); // 先加入第一个区间
    }

    for (size_t i = 1; i < ranges.size(); ++i) {
        // 当前区间的起始地址和结束地址（[start, end)）
        const auto &current = ranges[i];

        // 最后一个已合并区间的起始地址和结束地址
        auto &last = merged.back();
        uint64_t lastEnd = last.addr + last.size;
        // 判断是否重叠或相邻
        if (current.addr <= lastEnd) {
            // 合并：取最小start和最大end
            last.size = std::max(lastEnd, current.addr + current.size) - last.addr;
        } else {
            // 不重叠，直接加入
            merged.push_back(current);
        }
    }

    size_t memorySize = merged.size();
    /// globalHead处的hostMemoryNum数量会大于合并之后的内存，kernel侧越界算法会遍历所有hostMemoryNum，用于做越界判断；
    /// 故这里多余的数量需要用空信息填充，保证kernel侧越界算法逻辑正确；
    if (memorySize < mallocCount_) {
        merged.insert(merged.end(), mallocCount_ - memorySize, {});
    } else if (memorySize > mallocCount_) {
        ERROR_LOG("memorySize :%lu exceeds mallocCount :%u", memorySize, static_cast<uint32_t>(mallocCount_));
    }
    return merged;
}

void MemoryManage::UpdateMemoryOp()
{
    for (auto it = memoryOpAddrInfos_.begin(); it != memoryOpAddrInfos_.end();) {
        if (it->addr != 0x0 && it->length != 0) {
            it++;
        } else {
            it = memoryOpAddrInfos_.erase(it);
        }
    }
    mallocCount_ = memoryOpAddrInfos_.size();
}

void ReportMalloc(uint64_t addr, uint64_t size, MemInfoSrc memInfoSrc, MemInfoDesc memInfoDesc, uint64_t paramsNo)
{
    PacketHead head = { PacketType::MEMORY_RECORD };
    HostMemRecord record{};
    record.type = MemOpType::MALLOC;
    record.infoSrc = memInfoSrc;
    record.dstAddr = addr;
    record.memSize = size;
    record.infoDesc = memInfoDesc;
    record.paramsNo = paramsNo;
    MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(record.dstAddr, record.infoSrc, record.memSize);
    LocalDevice::Local().Notify(Serialize(head, record));
}

void ReportFree(uint64_t addr, MemInfoSrc memInfoSrc, MemInfoDesc memInfoDesc, uint64_t paramsNo)
{
    PacketHead head = { PacketType::MEMORY_RECORD };
    HostMemRecord record{};
    record.type = MemOpType::FREE;
    record.infoSrc = memInfoSrc;
    record.dstAddr = addr;
    record.infoDesc = memInfoDesc;
    record.paramsNo = paramsNo;
    MemoryManage::Instance().CacheMemory<MemoryOpType::FREE>(record.dstAddr, record.infoSrc);
    LocalDevice::Local().Notify(Serialize(head, record));
}

void ReportMemset(uint64_t addr, uint64_t size, MemInfoSrc memInfoSrc, MemInfoDesc memInfoDesc, uint64_t paramsNo)
{
    PacketHead head = { PacketType::MEMORY_RECORD };
    HostMemRecord record{};
    record.type = MemOpType::STORE;
    record.infoSrc = memInfoSrc;
    record.dstAddr = addr;
    record.memSize = size;
    record.infoDesc = memInfoDesc;
    record.paramsNo = paramsNo;
    LocalDevice::Local().Notify(Serialize(head, record));
}

void ReportOpMallocInfo(const rtArgsEx_t *const argsInfo, KernelContext::OpMemInfo &opMemInfo)
{
    // 设置临时数组记录顺序，临时map用于去重，目的是保持去重后顺序一致
    std::vector<uint64_t> order;
    std::unordered_map<uint64_t, KernelContext::AddrInfo> addrMap;
    if (argsInfo == nullptr || argsInfo->args == nullptr) { return; }
    auto updateAddrInfos = [&](const KernelContext::AddrInfo &addrInfo) {
        std::pair<decltype(addrMap)::iterator, bool> it = addrMap.insert(std::make_pair(addrInfo.addr, addrInfo));
        // 部分算子存在内存复用情况，输入和输出为相同地址，这里计算相同地址的最大长度作为malloc上报时的长度
        auto &addrData = it.first->second;
        if (!it.second && addrInfo.length > addrData.length) { addrData.length = addrInfo.length; }
        if (it.second) { order.push_back(addrInfo.addr); }
    };
    auto *buff = static_cast<uint64_t *>(argsInfo->args);
    uint32_t index = 0U;
    for (auto &it : opMemInfo.inputParamsAddrInfos) {
        it.addr = *(buff + opMemInfo.skipNum + index);
        it.paramsNo -= opMemInfo.skipNum + KernelContext::Instance().GetMc2CtxFlag();
        KernelContext::Instance().ParseSecondPtrAddrs(*argsInfo, opMemInfo, index);
        if (ProcessMc2CtxAddr(index, it.addr, updateAddrInfos)) {
            updateAddrInfos(it);
        }
        index++;
    }

    for (const auto &infos : opMemInfo.secondPtrAddrInfos) {
        for (const auto &addrInfo : infos.second.addrInfoVec) {
            updateAddrInfos(addrInfo);
        }
    }
    for (uint64_t address : order) {
        if (addrMap[address].addr != 0x00 && addrMap[address].length != 0x00) {
            const auto &iter = addrMap[address];
            opMemInfo.uniqueAddrInfos.push_back({address, iter.length, iter.memInfoSrc,
                iter.memInfoDesc, iter.paramsNo});
        }
    }

    for (const auto &it : opMemInfo.uniqueAddrInfos) {
        ReportMalloc(it.addr, it.length, it.memInfoSrc, it.memInfoDesc, it.paramsNo);
        // 内存分配是通过获取tensor信息模拟的，无实际的桩函数记录，因此需要设为STORE，防止未初始化读误报
        ReportMemset(it.addr, it.length, it.memInfoSrc, it.memInfoDesc, it.paramsNo);
    }

    // 上报tiling地址信息
    if (argsInfo->hasTiling == 0U) { return; }
    uint64_t tilingAddr = *(buff + argsInfo->tilingAddrOffset / 8);
    /// adump接口更新后，tiling末尾会多一些shape信息，此时，tilingData的准确长度为从.o解析出的长度
    /// 为了保证兼容性，这里取max。为了适配DFX，对齐到 32 字节后再进行检测。
    if (argsInfo->argsSize < argsInfo->tilingDataOffset) { return; }
    uint64_t tilingSize = (std::max(static_cast<uint64_t>(argsInfo->argsSize - argsInfo->tilingDataOffset),
        opMemInfo.tilingDataSize) + 31U) / 32U * 32U;  // 加 31 是为了做 32B 对齐处理。

    // 输入输出tensor有两种上报路径，如果不通过setExeptionExtInfo上报，那么通过rtMalloc拿到tensor信息
    // tilling信息总是通过RtKernelLaunchWithHandleV2获取，此时需要把tillin信息上报来源设置为DEVICE
    // 和tensor信息保持一致
    MemInfoSrc memInfoSrc = MemInfoSrc::BYPASS;
    ReportMalloc(tilingAddr, tilingSize, memInfoSrc, MemInfoDesc::TILING);
    // 内存分配是通过获取tilling信息模拟的，无实际的桩函数记录，因此需要设为STORE，防止未初始化读误报
    ReportMemset(tilingAddr, tilingSize, memInfoSrc, MemInfoDesc::TILING);
    // tiling 信息是 BYPASS 类型，释放时需要在 EXTRA 信息之前
    opMemInfo.uniqueAddrInfos.insert(opMemInfo.uniqueAddrInfos.begin(), {tilingAddr, tilingSize, memInfoSrc,
        MemInfoDesc::TILING});
}

void ReportOpFreeInfo(KernelContext::OpMemInfo &opMemInfo)
{
    for (const auto &it : opMemInfo.uniqueAddrInfos) {
        ReportFree(it.addr, it.memInfoSrc, it.memInfoDesc, it.paramsNo);
    }

    opMemInfo.Clear();
    MemoryManage::Instance().UpdateMemoryOp();
}

void ReportOpMallocInfo(const AclrtLaunchArgsInfo &launchArgs, OpMemInfo &opMemInfo)
{
    if (launchArgs.hostArgs == nullptr) {
        WARN_LOG("hostArgs is nullptr");
        return;
    }
    // 设置临时数组记录顺序，临时map用于去重，目的是保持去重后顺序一致
    std::vector<uint64_t> order;
    std::unordered_map<uint64_t, AddrInfo> addrMap;
    auto updateAddrInfos = [&](const AddrInfo &addrInfo) {
        std::pair<decltype(addrMap)::iterator, bool> it = addrMap.insert(std::make_pair(addrInfo.addr, addrInfo));
        // 部分算子存在内存复用情况，输入和输出为相同地址，这里计算相同地址的最大长度作为malloc上报时的长度
        auto &addrData = it.first->second;
        if (!it.second && addrInfo.length > addrData.length) { addrData.length = addrInfo.length; }
        if (it.second) { order.push_back(addrInfo.addr); }
    };
    auto *buff = static_cast<uint64_t *>(launchArgs.hostArgs);
    uint32_t index = 0U;
    for (auto &it : opMemInfo.inputParamsAddrInfos) {
        it.addr = *(buff + opMemInfo.skipNum + index);
        ArgsManager::Instance().ParseSecondPtrAddrs(launchArgs, opMemInfo, index);
        if (ProcessMc2CtxAddr(index, it.addr, updateAddrInfos)) { updateAddrInfos(it); }
        index++;
    }

    for (const auto &infos : opMemInfo.secondPtrAddrInfos) {
        for (const auto &addrInfo : infos.second.addrInfoVec) { updateAddrInfos(addrInfo); }
    }
    for (uint64_t address : order) {
        if (addrMap[address].addr != 0x00 && addrMap[address].length != 0x00) {
            const auto &iter = addrMap[address];
            opMemInfo.uniqueAddrInfos.push_back({address, iter.length, iter.memInfoSrc, iter.memInfoDesc, iter.paramsNo});
        }
    }

    for (const auto &it : opMemInfo.uniqueAddrInfos) {
        ReportMalloc(it.addr, it.length, it.memInfoSrc, it.memInfoDesc, it.paramsNo);
        // 内存分配是通过获取tensor信息模拟的，无实际的桩函数记录，因此需要设为STORE，防止未初始化读误报
        ReportMemset(it.addr, it.length, it.memInfoSrc, it.memInfoDesc, it.paramsNo);
    }
    // 为0时认为没有tiling信息，提前返回
    if (launchArgs.placeHolderNum == 0U) {
        return;
    }
    /// 所有placeHolderArray中最大的addrOffset对应的是tilingAddr地址偏移，此时的dataOffset为tilingData偏移
    uint32_t tilingDataOffset{};
    uint32_t tilingAddrOffset{};
    for (size_t i = 0; i < launchArgs.placeHolderNum; ++i) {
        if (launchArgs.placeHolderArray[i].addrOffset > tilingAddrOffset) {
            tilingAddrOffset = launchArgs.placeHolderArray[i].addrOffset;
            tilingDataOffset = launchArgs.placeHolderArray[i].dataOffset;
        }
    }
    uint64_t tilingAddr = buff[tilingAddrOffset / sizeof(uintptr_t)];
    uint64_t tilingArgsSize = launchArgs.argsSize - tilingDataOffset -
        (DeviceContext::Local().NeedOverflowStatus() ? sizeof(uintptr_t) : 0);
    uint64_t tilingSize = (std::max(tilingArgsSize, opMemInfo.tilingDataSize) + 31U) / 32U * 32U;  // 加 31 是为了做 32B 对齐处理。
    // 输入输出tensor有两种上报路径，如果不通过setExeptionExtInfo上报，那么通过rtMalloc拿到tensor信息
    // tilling信息总是通过RtKernelLaunchWithHandleV2获取，此时需要把tillin信息上报来源设置为DEVICE
    // 和tensor信息保持一致
    MemInfoSrc memInfoSrc = MemInfoSrc::BYPASS;
    ReportMalloc(tilingAddr, tilingSize, memInfoSrc, MemInfoDesc::TILING);
    // 内存分配是通过获取tilling信息模拟的，无实际的桩函数记录，因此需要设为STORE，防止未初始化读误报
    ReportMemset(tilingAddr, tilingSize, memInfoSrc, MemInfoDesc::TILING);
    // tiling 信息是 BYPASS 类型，释放时需要在 EXTRA 信息之前
    opMemInfo.uniqueAddrInfos.insert(opMemInfo.uniqueAddrInfos.begin(), {tilingAddr, tilingSize, memInfoSrc});
}

std::vector<Elf64_Shdr> GetAllocSectionHeaders(std::map<std::string, Elf64_Shdr> &headers)
{
    std::vector<Elf64_Shdr> allocHeaders;
    for (auto const &headerPair : headers) {
        // SHF_ALLOC 标志位为 1 说明该 section 在运行时的 GM 上需要分配对应的内存
        Elf64_Shdr const &header = headerPair.second;
        if ((header.sh_flags & SHF_ALLOC) > 0 && header.sh_size > 0) {
            DEBUG_LOG("need alloc section %s, addr: 0x%" PRIx64 ", size: %lu",
                      headerPair.first.c_str(), header.sh_addr, header.sh_size);
            allocHeaders.emplace_back(header);
        }
    }
    return allocHeaders;
}

void ReportSectionsMalloc(uint64_t pcStartAddr, std::vector<Elf64_Shdr> const &headers)
{
    if (pcStartAddr == 0x00) {
        // pcStartAddr should never equal to 0 during runtime
        ERROR_LOG("invalid pcStartAddr, ignore all of sections");
        return;
    }

    PacketHead head = { PacketType::MEMORY_RECORD };
    for (auto const & header : headers) {
        HostMemRecord record {};
        // section 的运行时地址可以由 pcStartAddr + sectionAddr 计算得到
        record.type = MemOpType::MALLOC;
        record.infoSrc = MemInfoSrc::BYPASS;
        record.dstAddr = pcStartAddr + header.sh_addr;
        record.memSize = header.sh_size;
        record.infoDesc = MemInfoDesc::SECTION;
        MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(record.dstAddr, record.infoSrc, record.memSize);
        LocalDevice::Local().Notify(Serialize(head, record));

        // 需要通过 memset 将这片内存标记为已初始化的状态，防止初始化检测产生误报
        record.type = MemOpType::STORE;
        LocalDevice::Local().Notify(Serialize(head, record));
    }
}

void ReportSectionsFree(uint64_t pcStartAddr, std::vector<Elf64_Shdr> const &headers)
{
    if (pcStartAddr == 0x00) {
        // pcStartAddr should never equal to 0 during runtime
        ERROR_LOG("invalid pcStartAddr, ignore all of sections");
        return;
    }

    PacketHead head = { PacketType::MEMORY_RECORD };
    for (auto const & header : headers) {
        if (pcStartAddr > UINT64_MAX - header.sh_addr) {
            ERROR_LOG("invalid section for dstAddr out of bound");
            continue;
        }
        HostMemRecord record {};
        record.type = MemOpType::FREE;
        record.infoSrc = MemInfoSrc::BYPASS;
        record.dstAddr = pcStartAddr + header.sh_addr;
        record.infoDesc = MemInfoDesc::SECTION;
        MemoryManage::Instance().CacheMemory<MemoryOpType::FREE>(record.dstAddr, record.infoSrc);
        LocalDevice::Local().Notify(Serialize(head, record));
    }
}

void ReportOverflowMalloc(KernelContext::OpMemInfo const &opMemInfo)
{
    if (!opMemInfo.hasOverflowAddr || opMemInfo.overflowAddr == 0UL) {
        return;
    }
    constexpr uint64_t overflowBufSize = 512;
    ReportMalloc(opMemInfo.overflowAddr, overflowBufSize, MemInfoSrc::BYPASS, MemInfoDesc::OVERFLOW_ADDR);
}

void ReportOverflowFree(KernelContext::OpMemInfo &opMemInfo)
{
    if (!opMemInfo.hasOverflowAddr || opMemInfo.overflowAddr == 0UL) {
        return;
    }
    ReportFree(opMemInfo.overflowAddr, MemInfoSrc::BYPASS, MemInfoDesc::OVERFLOW_ADDR);
}

bool IsMemoryOnDevice(void *ptr)
{
    struct DVattribute attr = {0};
    if (drvMemGetAttributeOrigin(ptr, &attr) != DRV_ERROR_NONE) {
        /// 使用非法地址时会导致获取地址空间失败，此时我们无法区分此地址为 host 侧或 device 侧
        /// 地址，为防止漏检始终对此类地址进行检测
        return true;
    }

    return attr.memType == DV_MEM_LOCK_DEV || attr.memType == DV_MEM_LOCK_DEV_DVPP;
}