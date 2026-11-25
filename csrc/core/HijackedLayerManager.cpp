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


#include "HijackedLayerManager.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "utils/InjectLogger.h"

namespace {

using LayerSet = std::unordered_set<std::string>;
using LayerMap = std::unordered_map<std::string, LayerSet>;

static LayerMap GetAclToRuntimeMap()
{
    return {
        {"aclrtSetDeviceImpl", {"rtSetDevice"}},
        {"aclrtResetDeviceImpl", {"rtDeviceReset"}},
        {"aclrtMallocImpl", {"rtMalloc"}},
        {"aclrtFreeImpl", {"rtFree"}},
        {"aclrtMemsetImpl", {"rtMemset"}},
        {"aclrtMemcpyImpl", {"rtMemcpy"}},
        {"aclrtMapMemImpl", {"rtMapMem"}},
        {"aclrtUnmapMemImpl", {"rtUnmapMem"}},
        {"aclrtIpcMemGetExportKeyImpl", {"rtIpcSetMemoryName"}},
        {"aclrtIpcMemImportByKeyImpl", {"rtIpcOpenMemory"}},
        {"aclrtIpcMemCloseImpl", {"rtIpcDestroyMemoryName", "rtIpcCloseMemory"}},
        {"aclrtGetDeviceImpl", {"rtGetDevice"}},
        {"aclrtCreateContextImpl", {"rtCtxCreateEx"}},
        {"aclrtQueryDeviceStatusImpl", {"rtDeviceStatusQuery"}},
        {"aclrtGetDeviceCountImpl", {"rtGetDeviceCount"}},
        {"aclrtGetDeviceInfoImpl", {"rtGetDeviceInfo"}},
        {"aclrtGetSocNameImpl", {"rtGetSocVersion"}},
        {"aclrtDeviceEnablePeerAccessImpl", {"rtEnableP2P"}},
        {"aclrtDeviceDisablePeerAccessImpl", {"rtDisableP2P"}},
    };
}

} // namespace [Dummy]

void HijackedLayerManager::Push(std::string const &func)
{
    this->layers_.push_front(func);
}

void HijackedLayerManager::Pop(void)
{
    if (!this->layers_.empty()) {
        this->layers_.pop_front();
    }
}

bool HijackedLayerManager::ParentInCallStack(std::string const &func) const
{
    if (this->layers_.empty()) {
        return false;
    }
    auto aclToRuntimeMap = GetAclToRuntimeMap();
    for (auto const &p : this->layers_) {
        auto it = aclToRuntimeMap.find(p);
        if (it == aclToRuntimeMap.cend()) {
            continue;
        }
        if (it->second.find(func) != it->second.cend()) {
            VERBOSE_LOG("lower layer function %s is masked by higher layer function %s",
                        func.c_str(), p.c_str());
            return true;
        }
    }
    return false;
}

bool HijackedLayerManager::InCallStack(std::string const &func) const
{
    return std::find(layers_.cbegin(), layers_.cend(), func) != layers_.cend();
}

LayerGuard::LayerGuard(HijackedLayerManager &manager, std::string const &func)
    : manager_{manager}
{
    manager.Push(func);
}

LayerGuard::~LayerGuard(void)
{
    this->manager_.Pop();
}
