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


#pragma once

#include <elf.h>
#include <map>
#include <string>
#include <vector>

#include "KernelContext.h"
#include "core/PlatformConfig.h"
#include "runtime.h"
#include "utils/Singleton.h"

enum class MemoryOpType : uint8_t {
    MALLOC = 0,
    FREE,
};

/* MemoryManage类主要负责管理算子内存的申请
 * 主要功能为将算子的所有内存申请合并为不连续的内存记录，然后按照升序排列之后返回
 *
 * 使用方法如下
 * @code
 * MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(addr, infoSrc, size)
 * MemoryManage::Instance().CacheMemory<MemoryOpType::FREE>(addr, infoSrc)
 * addrsVec = MemoryManage::Instance().MergeHostMems();
 * @endcode
 */

class MemoryManage : public Singleton<MemoryManage, false> {
public:
    friend class Singleton<MemoryManage, false>;
    using AddrsVec = std::vector<HostMemoryInfo>;
    using AddrsSet = std::set<HostMemoryInfo>;

    /**
    * @brief 缓存memoryOp信息，包含malloc和free
    * @param opType 内存分配的类型
    * @param addr 内存分配的地址
    * @param memInfoSrc 内存分配事件来源，用于工具侧进行信息层级控制
    * @param size  内存分配的长度
    * @param isUpdateCount  是否更新内存数量计数器，部分malloc内存已提前分配占位符，此种情况再调用此函数不需要在额外更新计数器
    */
    template <MemoryOpType opType>
    void CacheMemory(uint64_t addr, MemInfoSrc infoSrc, uint64_t size = 0, bool isUpdateCount = true);

    /**
    * @brief 缓存memory count
    * @param count 需要缓存的memory数量
    */
    void CacheMemoryCount(uint32_t count) { mallocCount_ += count; }

    // 合并所有的host侧memory信息，合并成不连续升序的形式；
    AddrsVec MergeHostMems();

    // 获取malloc内存数量，用于填充sanitizer_init开头
    uint32_t GetMallocCount() const { return mallocCount_; }

    // 更新内存信息，主要用于释放extra信息后，提取有效的内存地址放入memoryOpAddrInfos_中并更新mallocCount_
    void UpdateMemoryOp();

private:
    void ProcessMstxHeapMem(AddrsSet &cacheRtAclMem, const KernelContext::AddrInfo &addrInfo) const;
    AddrsVec ExtractValidMems(std::set<KernelContext::AddrInfo> &addrInfos) const;

    void Reset()
    {
        memoryOpAddrInfos_.clear();
        mallocCount_ = 0;
    }

    std::mutex memoryMutex_;
    // 算子运行时获取到的malloc信息，用于限定kernel侧检测时的gm合理范围
    std::set<KernelContext::AddrInfo> memoryOpAddrInfos_;
    std::atomic<uint32_t> mallocCount_{}; // 缓存的内存记录数量
};

/**
 * @brief 上报内存分配信息
 * @param addr 内存分配的地址
 * @param size  内存分配的长度
 * @param memInfoSrc 内存分配事件来源，用于工具侧进行信息层级控制
 * @param memInfoDesc 内存信息描述，用于描述当前地址是input/tiling之类
 * @param paramsNo 当前地址位于kernel入参的index
 */
void ReportMalloc(uint64_t addr, uint64_t size, MemInfoSrc memInfoSrc, MemInfoDesc memInfoDesc = MemInfoDesc::DEFAULT,
    uint64_t paramsNo = 0);

/**
 * @brief 上报内存释放信息
 * @param addr 内存释放的地址
 * @param memInfoSrc 内存释放事件来源，用于工具侧进行信息层级控制
 * @param memInfoDesc 内存信息描述，用于描述当前地址是input/tiling之类
 * @param paramsNo 当前地址位于kernel入参的index
 */
void ReportFree(uint64_t addr, MemInfoSrc memInfoSrc, MemInfoDesc memInfoDesc = MemInfoDesc::DEFAULT,
    uint64_t paramsNo = 0);

/**
 * @brief 上报内存初始化信息
 * @param addr 内存初始化的地址
 * @param size 内存初始化的长度
 * @param memInfoSrc 内存初始化事件来源，用于工具侧进行信息层级控制
 * @param memInfoDesc 内存信息描述，用于描述当前地址是input/tiling之类
 * @param paramsNo 当前地址位于kernel入参的index
 */
void ReportMemset(uint64_t addr, uint64_t size, MemInfoSrc memInfoSrc, MemInfoDesc memInfoDesc = MemInfoDesc::DEFAULT,
    uint64_t paramsNo = 0);

/**
 * @brief 上报算子内存分配信息，kernelLaunch老接口
 * @param argsInfo 算子入参信息，用于获取 tensor 地址
 * @param opMemInfo 保存 tensor 地址和长度信息，长度信息在 rtSetException 接口中获取
 */
void ReportOpMallocInfo(const rtArgsEx_t * const argsInfo, KernelContext::OpMemInfo &opMemInfo);

/**
 * @brief 上报算子内存释放信息
 * @param opMemInfo tensor 地址和长度信息，opMemInfo 在 ReportOpMallocInfo 中完成地址和长度的组装
 */
void ReportOpFreeInfo(KernelContext::OpMemInfo &opMemInfo);

/**
 * @brief 上报算子内存分配信息，kernelLaunch新接口
 * @param launchArgs 算子入参信息，用于获取 tensor 地址
 * @param opMemInfo 保存 tensor 地址和长度信息，长度信息在 adump 接口中获取
 */
void ReportOpMallocInfo(const AclrtLaunchArgsInfo &launchArgs, OpMemInfo &opMemInfo);

/**
 * @brief 获取段信息中需要内存分配的段
 * @param headers 二进制的段信息，其中一些属性为 Alloc 的段需要进行内存分配
 */
std::vector<Elf64_Shdr> GetAllocSectionHeaders(std::map<std::string, Elf64_Shdr> &headers);

/**
 * @brief 上报段内存分配记录，表示二进制开始执行时需要占用一定内存
 * @param pcStartAddr 运行时基地址，二进制段在内存上的实际地址为 pcStartAddr + header.sh_addr
 * @param headers 要上报内存分配的段信息列表
 */
void ReportSectionsMalloc(uint64_t pcStartAddr, std::vector<Elf64_Shdr> const &headers);

/**
 * @brief 上报段内存释放记录，表示二进制执行结束后释放对应的段内存
 * @param pcStartAddr 运行时基地址，二进制段在内存上的实际地址为 pcStartAddr + header.sh_addr
 * @param headers 要上报内存释放的段信息列表
 */
void ReportSectionsFree(uint64_t pcStartAddr, std::vector<Elf64_Shdr> const &headers);

/**
 * @brief 上报 AscendC overflow 地址内存分配记录，防止算子执行结束时向该地址写入标志位时
 * 工具报告非法写
 */
void ReportOverflowMalloc(KernelContext::OpMemInfo const &opMemInfo);

/**
 * @brief 上报 AscendC overflow 地址内存释放记录
 */
void ReportOverflowFree(KernelContext::OpMemInfo &opMemInfo);

/**
 * @brief 通过驱动接口判断内存是否在 device 上
 */
bool IsMemoryOnDevice(void *ptr);