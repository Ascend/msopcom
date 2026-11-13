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
#include "runtime/inject_helpers/MsTx.h"
#include "utils/InjectLogger.h"
#include "runtime.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/ThreadContext.h"
#include "utils/ElfLoader.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"
#include "ArgsManager.h"
#include "InteractHelper.h"

namespace {

// 映射只保存原分配身份，分配成功释放/重置时主动移除，地址复用不继承权限
// HCCL的静态对象析构晚于函数内静态表析构，仍会调用被劫持的Free
// 进程级登记和锁保留到操作系统回收，避免退出阶段访问已析构的容器
struct MemoryManageProcessState {
    std::mutex instancesMutex;
    std::unordered_map<int32_t, MemoryManage> instances; // key：deviceId
    std::mutex p2pMutex;
    std::vector<P2PMappingRecord> p2pMappings;
};

MemoryManageProcessState &MemoryProcessState()
{
    static auto *state = new MemoryManageProcessState;
    return *state;
}

std::mutex &p2pMutex = MemoryProcessState().p2pMutex;
std::vector<P2PMappingRecord> &p2pMappings = MemoryProcessState().p2pMappings;

void RevokeMappingsOfAllocation(int32_t ownerDeviceId, uint64_t ownerAddr) {
    std::lock_guard<std::mutex> lock(p2pMutex);
    p2pMappings.erase(std::remove_if(p2pMappings.begin(), p2pMappings.end(),
                          [ownerDeviceId, ownerAddr](const P2PMappingRecord &m) {
                              return m.ownerDeviceId == ownerDeviceId && m.ownerAddr == ownerAddr;
                          }),
        p2pMappings.end());
}

/// 函数返回值表示是否缓存当前传入的addr
bool ProcessMc2CtxAddr(uint64_t addr, std::function<void(const KernelContext::AddrInfo &addrInfo)> func) {
    std::vector<KernelContext::AddrInfo> mc2CtxAddrs;
    mc2CtxAddrs = ArgsManager::Instance().GetMc2AddrInfosFromArgAddr(addr);
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
    if (addr > UINT64_MAX - size || thresholdAddr > UINT64_MAX - thresholdSize) {
        return false;
    }
    return addr >= thresholdAddr && addr + size <= thresholdAddr + thresholdSize;
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
    std::lock_guard<std::mutex> lock(memoryMutex_);
    if (isUpdateCount) {
        mallocCount_++;
    }
    AddrInfo addrInfo{};
    addrInfo.addr = addr;
    addrInfo.length = size;
    addrInfo.memInfoSrc = infoSrc;
    addrInfo.permission = MstxAPI::MSTX_MEM_PERMISSIONS_REGION_FLAGS_DEFAULT;
    this->memoryOpAddrInfos_.insert(addrInfo);
}

template <>
void MemoryManage::CacheMemory<MemoryOpType::FREE>(uint64_t addr, MemInfoSrc infoSrc,
    uint64_t size, bool isUpdateCount)
{
    std::lock_guard<std::mutex> lock(memoryMutex_);
    (void)size;
    (void)isUpdateCount;
    for (const auto &it : this->memoryOpAddrInfos_) {
        if (it.addr == addr) {
            if (IsSameMemInfoSrc(it.memInfoSrc, infoSrc)) {
                // 撤销当前分配的精确映射窗口
                if (infoSrc == MemInfoSrc::ACL || infoSrc == MemInfoSrc::RT) {
                    RevokeMappingsOfAllocation(deviceId_, addr);
                }
                this->memoryOpAddrInfos_.erase(it);
                mallocCount_--;
                return;
            }
        }
    }
    WARN_LOG("free addr:%lx is not found, maybe the memory is already freed, infoSrc:%u", addr,
        static_cast<uint32_t>(infoSrc));
}

void MemoryManage::SetPermission(uint64_t addr, uint64_t size, uint32_t permission)
{
    std::lock_guard<std::mutex> lock(memoryMutex_);
    for (auto it = memoryOpAddrInfos_.begin(); it != memoryOpAddrInfos_.end(); ++it) {
        if (it->addr == addr && it->length == size) {
            AddrInfo addrInfo{*it};
            addrInfo.permission = permission;
            it = memoryOpAddrInfos_.erase(it);
            it = memoryOpAddrInfos_.insert(addrInfo).first;
        }
    }
}

void MemoryManage::ProcessMstxHeapMem(AddrsSet &cacheRtAclMem, const KernelContext::AddrInfo &addrInfo) const
{
    for (auto it = cacheRtAclMem.begin(); it != cacheRtAclMem.end();) {
        /// 如果在范围内，则rtMalloc的范围和mstx的差集即为当前rtMalloc的合法范围
        if (IsAddrInRange(addrInfo.addr, addrInfo.length, it->addr, it->size)) {
            if (addrInfo.addr > it->addr) {
                cacheRtAclMem.insert({it->addr, addrInfo.addr - it->addr, addrInfo.permission});
            }
            uint64_t rightSize = it->addr - addrInfo.addr + it->size - addrInfo.length;
            if (rightSize > 0) {
                cacheRtAclMem.insert({addrInfo.addr + addrInfo.length, rightSize, addrInfo.permission});
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
            ranges.push_back({addrs.addr, addrs.length, addrs.permission});
            continue;
        }
        if (isExtra) { continue; }

        if (infoSrc == MemInfoSrc::ACL || infoSrc == MemInfoSrc::RT) {
            cacheRtAclMems.insert({addrs.addr, addrs.length, addrs.permission});
        } else if (infoSrc == MemInfoSrc::MSTX_HEAP) {
            ProcessMstxHeapMem(cacheRtAclMems, addrs);
        } else {
            ranges.push_back({addrs.addr, addrs.length, addrs.permission});
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

MemoryManage::AddrsVec MemoryManage::SnapshotOnlineRanges() {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    return ExtractValidMems(memoryOpAddrInfos_);
}

void MemoryManage::UpdateMemoryOp()
{
    std::lock_guard<std::mutex> lock(memoryMutex_);
    for (auto it = memoryOpAddrInfos_.begin(); it != memoryOpAddrInfos_.end();) {
        if (it->addr != 0x0 && it->length != 0) {
            it++;
        } else {
            it = memoryOpAddrInfos_.erase(it);
        }
    }
    mallocCount_ = memoryOpAddrInfos_.size();
}

MemoryManage &MemoryManage::Instance() { return Instance(ThreadContext::Instance().GetDeviceId()); }

std::unordered_map<int32_t, MemoryManage> &MemoryManage::Instances()
{
    return MemoryProcessState().instances;
}

std::mutex &MemoryManage::InstanceMutex()
{
    return MemoryProcessState().instancesMutex;
}

MemoryManage &MemoryManage::Instance(int32_t deviceId)
{
    std::lock_guard<std::mutex> lock(InstanceMutex());
    auto &instances = Instances();
    auto found = instances.find(deviceId);
    if (found == instances.end()) {
        auto &instance = instances[deviceId];
        instance.deviceId_ = deviceId; // 发布后不再修改，避免并发读取设备归属时产生数据竞争
        return instance;
    }
    return found->second;
}

bool MemoryManage::ApplyP2PMapping(int32_t deviceId, uint64_t addr, uint64_t size)
{
    if (deviceId < 0 || addr == 0 || size == 0 || addr > UINT64_MAX - size) {
        return false;
    }
    // 映射建立不是每访存路径：持有实例锁及现有分配表锁，原子查找唯一所有方
    // 其它路径只持有单张分配表锁；取映射锁后不再反向获取分配表锁
    std::lock_guard<std::mutex> instancesLock(InstanceMutex());
    std::vector<std::unique_lock<std::mutex>> allocationLocks;
    for (auto &entry : Instances()) {
        allocationLocks.emplace_back(entry.second.memoryMutex_);
    }

    // 遍历所有设备的分配表，找"完整覆盖该窗口"的 ACL/RT 分配
    P2PMappingRecord record{};
    bool found = false;
    for (auto &entry : Instances()) {
        // 跳过使用方自己和非法设备
        if (entry.first == deviceId || entry.first < 0) {
            continue;
        }
        for (const auto &allocation : entry.second.memoryOpAddrInfos_) {
            if ((allocation.memInfoSrc != MemInfoSrc::ACL && allocation.memInfoSrc != MemInfoSrc::RT) ||
                !IsAddrInRange(addr, size, allocation.addr, allocation.length)) {
                continue;
            }
            if (found) {
                return false; // 同一地址存在多个候选时不猜测所有方
            }
            record = {deviceId, entry.first, addr, size, allocation.addr}; // 构造映射记录
            found = true;
        }
    }
    if (found) {
        std::lock_guard<std::mutex> mappingLock(p2pMutex);
        auto same = [&record](const P2PMappingRecord &m) {
            return m.deviceId == record.deviceId && m.ownerDeviceId == record.ownerDeviceId &&
                m.addr == record.addr && m.size == record.size && m.ownerAddr == record.ownerAddr;
        };
        // 先去重，再push_back
        if (std::find_if(p2pMappings.begin(), p2pMappings.end(), same) == p2pMappings.end()) {
            p2pMappings.push_back(record);
            // 在释放分配表锁前发布，避免Free事件排在映射之前
            InteractHelper::Notify(deviceId, PacketType::P2P_MAPPING_RECORD, record);
        }
        DEBUG_LOG("P2P mapping: device=%d owner=%d base=0x%lx addr=0x%lx size=%lu",
            deviceId, record.ownerDeviceId, record.ownerAddr, addr, size);
        return true;
    }
    return false;
}

std::vector<PeerMemoryInfo> MemoryManage::SnapshotP2PMappings(int32_t deviceId)
{
    // 先复制映射表，避免后续处理长时间持有 p2pMutex
    std::vector<P2PMappingRecord> mappings;
    {
        std::lock_guard<std::mutex> lock(p2pMutex);
        mappings = p2pMappings;
    }
    std::vector<PeerMemoryInfo> result;
    for (const auto &m : mappings) {
        // 只处理以本设备为使用方的映射
        if (m.deviceId != deviceId) {
            continue;
        }
        auto &owner = Instance(m.ownerDeviceId);
        std::lock_guard<std::mutex> allocationLock(owner.memoryMutex_);
        // 取得分配锁后再核对映射仍存活，防止快照复制后发生Free/重分配
        std::lock_guard<std::mutex> mappingLock(p2pMutex);
        bool active = std::any_of(p2pMappings.begin(), p2pMappings.end(), [&m](const P2PMappingRecord &current) {
            return current.deviceId == m.deviceId && current.ownerDeviceId == m.ownerDeviceId &&
                current.addr == m.addr && current.size == m.size && current.ownerAddr == m.ownerAddr;
        });
        if (!active) {
            continue;
        }
        // 沿用在线范围的来源筛选及用户权限，映射不能绕过EXTRA/MSTX等已有约束
        for (const auto &range : owner.ExtractValidMems(owner.memoryOpAddrInfos_)) {
            if (range.addr > UINT64_MAX - range.size) {
                continue;
            }
            const uint64_t begin = std::max(m.addr, range.addr);
            const uint64_t end = std::min(m.addr + m.size, range.addr + range.size);
            if (begin < end) {
                result.push_back({begin, end - begin, range.permission, m.ownerDeviceId});
            }
        }
    }
    return result;
}

void MemoryManage::ResetDevice(int32_t deviceId)
{
    auto &memory = Instance(deviceId);
    std::lock_guard<std::mutex> allocationLock(memory.memoryMutex_);
    std::lock_guard<std::mutex> mappingLock(p2pMutex);
    // 撤销该设备作为使用方或所有方的全部映射
    p2pMappings.erase(std::remove_if(p2pMappings.begin(), p2pMappings.end(), [deviceId](const P2PMappingRecord &m) {
        return m.deviceId == deviceId || m.ownerDeviceId == deviceId;
    }), p2pMappings.end());
    // 再清空本设备分配表
    memory.Reset();
}

void MemoryManage::RevokeP2PMappings(int32_t deviceId, int32_t ownerDeviceId)
{
    std::lock_guard<std::mutex> lock(p2pMutex);
    p2pMappings.erase(std::remove_if(p2pMappings.begin(), p2pMappings.end(),
        [deviceId, ownerDeviceId](const P2PMappingRecord &m) {
            return m.deviceId == deviceId && m.ownerDeviceId == ownerDeviceId;
        }), p2pMappings.end());
}

PeerAccessState &PeerAccessState::Instance() {
    static PeerAccessState state; // 单进程唯一，所有设备共享
    return state;
}

void PeerAccessState::Apply(int32_t deviceId, int32_t peerDeviceId, PeerAccessOp operation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (deviceId < 0) {
        return;
    }
    if (operation == PeerAccessOp::RESET) {
        // 双向删除
        relations_.erase(deviceId);
        for (auto &entry : relations_) {
            entry.second.erase(deviceId);
        }
    } else if (peerDeviceId >= 0 && peerDeviceId != deviceId) {
        if (operation == PeerAccessOp::ENABLE) {
            relations_[deviceId].insert(peerDeviceId);
        } else if (operation == PeerAccessOp::DISABLE) {
            relations_[deviceId].erase(peerDeviceId);
        }
    }
}

std::vector<PeerMemoryInfo> PeerAccessState::Snapshot(int32_t deviceId) {
    // 加锁保证收集期间授权关系不变
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PeerMemoryInfo> result;
    auto it = relations_.find(deviceId);
    if (it == relations_.end()) {
        return result;
    }
    for (int32_t peer : it->second) {
        // 各个目标设备的内存表分别在 SnapshotOnlineRanges 内部加锁读取
        for (const auto &memory : MemoryManage::Instance(peer).SnapshotOnlineRanges()) {
            if (memory.addr != 0 && memory.size != 0) {
                result.push_back({memory.addr, memory.size, memory.permission, peer});
            }
        }
    }
    return result;
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
    // 在读取参数槽前校验完整范围，减法前先检查 skipNum，避免无符号下溢。
    const uint64_t slots = argsInfo->argsSize / sizeof(uint64_t);
    if (opMemInfo.skipNum > slots || opMemInfo.inputParamsAddrInfos.size() > slots - opMemInfo.skipNum) {
        WARN_LOG("Operator input slots exceed the argument buffer; skipping memory collection.");
        return;
    }
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
        // 参数编号和 MC2 context 过滤均使用传入元数据的标志，避免依赖已被清空的实时标志。
        const uint64_t hiddenSlots = static_cast<uint64_t>(opMemInfo.skipNum) + opMemInfo.hasMc2Ctx;
        // MC2 context 和部分外部元数据用 0 表示无参数编号；不能将它减成 UINT64_MAX。
        it.paramsNo = it.paramsNo > hiddenSlots ? it.paramsNo - hiddenSlots : 0;
        KernelContext::Instance().ParseSecondPtrAddrs(*argsInfo, opMemInfo, index);
        // 普通参数直接缓存；MC2 context 先交给专用逻辑处理，再由其返回值决定是否缓存 context 地址。
        if (!opMemInfo.hasMc2Ctx || index != MC2_CONTEXT_PARAMS_INDEX ||
            ProcessMc2CtxAddr(it.addr, updateAddrInfos)) {
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
    // tiling 地址也是参数缓冲区中的一个完整槽，必须先验证边界再读取。
    if (argsInfo->tilingAddrOffset > argsInfo->argsSize ||
        argsInfo->argsSize - argsInfo->tilingAddrOffset < sizeof(uint64_t)) { return; }
    uint64_t tilingAddr = 0;
    std::memcpy(&tilingAddr, static_cast<const uint8_t *>(argsInfo->args) + argsInfo->tilingAddrOffset,
        sizeof(tilingAddr));
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
        DEBUG_LOG("mc2? addr=%#lx, size=%lu\n", addrInfo.addr, addrInfo.length);
        if (it.second) { order.push_back(addrInfo.addr); }
    };
    auto *buff = static_cast<uint64_t *>(launchArgs.hostArgs);
    uint32_t index = 0U;
    DEBUG_LOG("opMemInfo inputParamsAddrInfos size=%lu\n", opMemInfo.inputParamsAddrInfos.size());
    for (auto &it : opMemInfo.inputParamsAddrInfos) {
        it.addr = *(buff + opMemInfo.skipNum + index);
        ArgsManager::Instance().ParseSecondPtrAddrs(launchArgs, opMemInfo, index);
        // 普通参数直接缓存；MC2 context 先交给专用逻辑处理，再由其返回值决定是否缓存 context 地址。
        if (!opMemInfo.hasMc2Ctx || index != MC2_CONTEXT_PARAMS_INDEX || ProcessMc2CtxAddr(it.addr, updateAddrInfos)) {
            updateAddrInfos(it);
        }
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
        // ArgsArray 路径无 placeholder，但若 meta 解析出 tiling 的 paramsNo，
        // 调用方会把 tiling 指针在 hostArgs 中的偏移填入 tilingAddrOffset，
        // 此时仍需上报 tiling；否则确实没有 tiling 信息。
        if (launchArgs.tilingAddrOffset == 0U) {
            return;
        }
        uint64_t tilingAddr = buff[launchArgs.tilingAddrOffset / sizeof(uintptr_t)];
        if (tilingAddr == 0U || opMemInfo.tilingDataSize == 0U) {
            return;
        }
        // ArgsArray 的 hostArgs 只含各参数指针值，不含 tiling 数据区，
        // 因此 tiling 长度只能取 meta 解析出的 tilingDataSize（对齐 32B）。
        uint64_t tilingSize = (opMemInfo.tilingDataSize + 31U) / 32U * 32U;
        MemInfoSrc memInfoSrc = MemInfoSrc::BYPASS;
        ReportMalloc(tilingAddr, tilingSize, memInfoSrc, MemInfoDesc::TILING);
        ReportMemset(tilingAddr, tilingSize, memInfoSrc, MemInfoDesc::TILING);
        opMemInfo.uniqueAddrInfos.insert(
            opMemInfo.uniqueAddrInfos.begin(), {tilingAddr, tilingSize, memInfoSrc, MemInfoDesc::TILING});
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
