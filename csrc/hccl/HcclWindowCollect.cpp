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

#include "HcclWindowCollect.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <utility>
#include <vector>
#include "HcclOrigin.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "utils/InjectLogger.h"

namespace {
using Windows = std::vector<std::pair<uint64_t, uint64_t>>;

struct CommWindowState {
    Windows observed; // 最近一次查询到的窗口，用于识别窗口替换
    // 当前observed列表下成功登记过的历史，不是实时有效映射表；Free后仍保留，防止相同查询重新授权
    Windows registered;
};
struct HcclProcessState {
    std::mutex mutex; // 同一通信域可能被多个Host线程使用；查询期间持锁，防止Destroy释放句柄
    // 按使用设备和通信域保存观察状态；DestroyHcclComm或ForgetHcclDevice负责删除
    std::map<std::pair<int32_t, HcclComm>, CommWindowState> windows;
};
HcclProcessState &ProcessState() {
    // HCCL可能在静态析构阶段释放设备；与分配表一样保留到进程回收。
    static auto *state = new HcclProcessState; // NOLINT(cppcoreguidelines-owning-memory)
    return *state;
}
}

void RememberHcclComm(int32_t deviceId, HcclComm comm) {
    if (deviceId < 0 || comm == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(ProcessState().mutex);
    // 重复获取名字不覆盖已有登记历史；这里只记通信域，尚不登记远端窗口
    ProcessState().windows.emplace(std::make_pair(deviceId, comm), CommWindowState{});
}

void RefreshHcclWindows(int32_t deviceId) {
    // 持进程通信域状态锁，遍历该使用设备的通信域
    std::lock_guard<std::mutex> lock(ProcessState().mutex);
    for (auto &entry : ProcessState().windows) {
        if (entry.first.first != deviceId) {
            continue;
        }
        HcclComm comm = entry.first.second;
        // scope随ApplyP2PMapping写入每条窗口记录，用于按设备和通信域撤销；它不是设备号或地址范围
        const uint64_t scope = reinterpret_cast<uint64_t>(comm);
        uint32_t rankSize = 0;
        Windows windows;
        if (HcclGetRankSizeOrigin(comm, &rankSize) == HCCL_SUCCESS) {
            // rank是域内成员序号，只用于查询窗口；owner设备由真实分配表匹配
            for (uint32_t rank = 0; rank < rankSize; ++rank) {
                void *addr = nullptr;
                uint64_t size = 0;
                if (CommGetRemoteCCLBufOrigin(comm, rank, &addr, &size) == HCCL_SUCCESS && addr != nullptr &&
                    size != 0) {
                    windows.emplace_back(reinterpret_cast<uint64_t>(addr), size);
                }
            }
        }
        // 查询失败的成员不进入windows；列表变化时撤销旧scope，再允许按新列表登记
        if (windows != entry.second.observed) {
            MemoryManage::RevokeScopedP2PMappings(deviceId, scope);
            entry.second.registered.clear();
            entry.second.observed = windows;
        }
        // 已成功登记的窗口不重复发布，即使Free已删除有效映射，也不凭相同查询复活
        for (const auto &window : windows) {
            if (std::find(entry.second.registered.begin(), entry.second.registered.end(), window) !=
                entry.second.registered.end()) {
                continue;
            }
            // 未匹配唯一有效owner则不记入registered，后续kernel启动可重试
            // 成功时发布带scope的精确映射，并记下登记历史；检测仍受owner有效范围和权限约束
            if (MemoryManage::ApplyP2PMapping(deviceId, window.first, window.second, scope)) {
                entry.second.registered.push_back(window);
                DEBUG_LOG(
                    "HCCL window: device=%d comm=%p addr=0x%lx size=%lu", deviceId, comm, window.first, window.second);
            }
        }
    }
}

HcclResult DestroyHcclComm(HcclComm comm) {
    // 与Refresh共用锁，原始销毁执行期间不允许查询该句柄
    std::lock_guard<std::mutex> lock(ProcessState().mutex);
    const HcclResult ret = HcclCommDestroyOrigin(comm);
    if (ret == HCCL_SUCCESS) {
        // RememberHcclComm的按域删除路径：先撤窗口，再删除各使用设备上的观察和登记历史
        for (auto it = ProcessState().windows.begin(); it != ProcessState().windows.end();) {
            if (it->first.second == comm) {
                MemoryManage::RevokeScopedP2PMappings(it->first.first, reinterpret_cast<uint64_t>(comm));
                it = ProcessState().windows.erase(it);
            } else {
                ++it;
            }
        }
    }
    return ret;
}

void ForgetHcclDevice(int32_t deviceId) {
    // RememberHcclComm的按设备删除路径：reset先忘记句柄，相关映射由MemoryManage::ResetDevice统一清理
    std::lock_guard<std::mutex> lock(ProcessState().mutex);
    for (auto it = ProcessState().windows.begin(); it != ProcessState().windows.end();) {
        if (it->first.first == deviceId) {
            it = ProcessState().windows.erase(it);
        } else {
            ++it;
        }
    }
}
