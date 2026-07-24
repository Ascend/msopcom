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
#include <mutex>
#include <map>

#include "runtime.h"
#include "acl_rt_impl/AscendclImplOrigin.h"

/*
 * MemoryContext 类用于
 * 1. 记录程序中所申请的内存信息，
 * 2. 为程序所使用的内存创建快照备份，
 * 3. 把内存恢复为快照备份的版本。
 *
 * 约束：
 * 1. 仅支持保存一个快照版本
 * 2. 仅支持HBM内存
 */
class MemoryContext {
    using MemAddr = void *;
public:
    static MemoryContext &Instance()
    {
        static thread_local MemoryContext inst; // 线程局部
        return inst;
    }

    /**
    * @brief 保存已申请到的GM内存信息
    *
    * @param  addr [IN] 内存地址
    * @param  size [IN] 内存大小
    * @param  type [IN] 内存类型
    * @param  moduleId [IN] 内存所属module id
    * @retval NA
    */
    void Append(const MemAddr addr, uint64_t size);

    /**
    * @brief 移除已记录的指定内存信息
    *
    * @param  addr [IN] 需释放的GM内存地址
    * @retval NA
    */
    void Discard(MemAddr addr);

    /**
    * @brief 为所有被记录的内存区域创造一份快照保存，若已有旧备份，则更新之
    *
    * @retval success return true
    * @retval failure return false
    */
    bool Backup();

    /**
    * @brief 把所有被记录的内存区域恢复为保存的快照版本
    *
    * @retval success return true
    * @retval failure return false
    */
    bool Restore();


private:
    MemoryContext() = default;
    ~MemoryContext();

    // 移除所有已记录的内存信息
    void DiscardAll();

    struct MemSection {
        MemAddr originAddr;
        MemAddr snapshotAddr;
        uint64_t size;
        aclrtMemMallocPolicy policy;
    };

    enum class MemCopyDirection : uint8_t {
        ORIGIN_TO_SNAPSHOT,
        SNAPSHOT_TO_ORIGIN,
    };

    // 为指定的内存块创建快照，若已有旧快照，则更新之
    bool CreateSnapshot(MemSection &section, aclrtStream stream = nullptr) const;

    // 把指定的内存块恢复为快照版本
    bool RestoreFromSnapshot(const MemSection &section, aclrtStream stream = nullptr) const;

    // 在快照内存与原始内存间搬运内存，异步搬运后进行流同步
    bool MemCopyWithStream(const MemSection &section, aclrtStream stream,
        MemCopyDirection dir) const;

    // 在快照内存与原始内存间搬运内存，同步拷贝
    bool MemCopySync(const MemSection &section, MemCopyDirection dir) const;

    aclrtStream GetCurrentStream();

    std::map<MemAddr, MemSection> memSectionMap_; // key: origin mem addr, val: mem section
    std::mutex mapMtx_;
    std::mutex streamMtx_;
    aclrtStream stream_{}; // 用于异步拷贝+流同步
};
