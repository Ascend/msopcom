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


#include "MsTx.h"

#include <algorithm>

#include "utils/Ustring.h"
#include "utils/InjectLogger.h"
#include "ProfDataCollect.h"

using namespace MstxAPI;

MsTx::MsTx()
{
    MstxAttrReset();
}

void MsTx::MstxAttrReset(bool forceReset)
{
    std::lock_guard<std::mutex> lock(mstxAttrMutex_);
    if (isMstxInit_ && !forceReset) {
        return;
    }
    mstxEnabledMessageList_ = {};
    mstxRangeMessageUsageCountMap_ = {};
    mstxRangeActiveId2MessageMap_ = {};
    mstxRangeId_ = 0;
    mstxName2DomainMap_.clear();
    mstxDomain2NameMap_.clear();
    mstxDomainMemHeapDescMap_.clear();
    mstxDomainMemHeapMap_.clear();
    mstxDomainRegionsMap_.clear();
    mstxRegionRangeDescMap_.clear();
    isMstxEnabledMessageInit_ = false;
    isMstxInit_ = true;
}

MstxRangeId MsTx::MstxRangeAdd(const std::string& message)
{
    bool valid = true;
    if (ProfConfig::Instance().IsRangeReplay()) {
        // if message not enable or another enable message add when an unfinished message in range replay mode, it will be invalid, cannot add to mstxRangeActiveId2MessageMap
        if (!IsMessageEnable(message)) {
            valid = false;
        } else if (IsInMstxRange()) {
            WARN_LOG("Mstx messages cannot overlap when --replay-mode=range, message [%s] range is invalid.", message.c_str());
            valid = false;
        }
    }
    std::lock_guard<std::mutex> lock(mstxAttrMutex_);
    if (mstxRangeId_ == UINT64_MAX) {
        WARN_LOG("The number of mstx range start api called reaches the limit.");
        return UINT64_MAX;
    }
    mstxRangeActiveId2MessageMap_[mstxRangeId_] = message;
    if (valid) {
        mstxRangeMessageUsageCountMap_[std::this_thread::get_id()][message]++;
    } else {
        rangeReplayInvalidRangeIds_.insert(mstxRangeId_);
    }
    return mstxRangeId_++;
}

void MsTx::MstxRangeEnd(const MstxRangeId& rangeId)
{
    auto threadId = std::this_thread::get_id();
    {
        std::lock_guard<std::mutex> lock(mstxAttrMutex_);
        if (mstxRangeActiveId2MessageMap_.find(rangeId) == mstxRangeActiveId2MessageMap_.end()) {
            WARN_LOG("mstxRangeId %lu is not registered or mstxRangeEnd may called multiple times. Please check.", rangeId);
            return;
        }
        std::string message = mstxRangeActiveId2MessageMap_[rangeId];
        mstxRangeActiveId2MessageMap_.erase(rangeId);
        if (rangeReplayInvalidRangeIds_.count(rangeId) > 0) {
            return;
        }
        if ((mstxRangeMessageUsageCountMap_.find(threadId) == mstxRangeMessageUsageCountMap_.end()) ||
            (mstxRangeMessageUsageCountMap_[threadId].find(message) == mstxRangeMessageUsageCountMap_[threadId].end())) {
            // Protection in abnormal scenarios, which usually does not occur: key==message not in map
            return;
        }
        if (mstxRangeMessageUsageCountMap_[threadId][message] > 0) {
            --mstxRangeMessageUsageCountMap_[threadId][message];
        }
        if (mstxRangeMessageUsageCountMap_[threadId][message] == 0) {
            mstxRangeMessageUsageCountMap_[threadId].erase(message);
        }
        if (mstxRangeMessageUsageCountMap_[threadId].empty()) {
            mstxRangeMessageUsageCountMap_.erase(threadId);
        }
    }

    // for last operator in every mstx range, call aclmdlRICaptureEndImpl
    if (ProfConfig::Instance().IsRangeReplay()) {
        auto rangeConfig = ProfDataCollect::GetThreadRangeConfigMap(threadId);
        if (!rangeConfig.flag || rangeConfig.stream == nullptr) {
            // without match aclmdlRICaptureBeginImpl, no need to call aclmdlRICaptureEndImpl
            return;
        }
        aclmdlRI modelRI;
        auto res = aclmdlRICaptureEndImplOrigin(rangeConfig.stream, &modelRI);
        if (res != ACL_ERROR_NONE) {
            ERROR_LOG("Range replay end failed, res is %d.", res);
            ProfDataCollect::ResetRangeConfig(threadId);
            aclmdlRIDestroyImplOrigin(modelRI);
            return;
        }
        ProfDataCollect collect(nullptr, false);
        collect.RangeReplay(rangeConfig.stream, modelRI);
        res = aclmdlRIDestroyImplOrigin(modelRI);
        if (res != ACL_ERROR_NONE) {
            WARN_LOG("Destroy range replay modelRI failed, res is %d.", res);
        }
    }
}

bool MsTx::IsInMstxRange()
{
    auto mstxProfConfig = ProfConfig::Instance().GetConfig().mstxProfConfig;
    if (!mstxProfConfig.isMstxEnable) {
        // --mstx=off same as always in range
        return true;
    }
    auto threadId = std::this_thread::get_id();
    std::string messageString = std::string(mstxProfConfig.mstxEnabledMessage);
    std::lock_guard<std::mutex> lock(mstxAttrMutex_);
    if (messageString.empty()) {
        // --mstx=on and --mstx-include is empty, default activate all message in this thread
        return (mstxRangeMessageUsageCountMap_.find(threadId) != mstxRangeMessageUsageCountMap_.end()) &&
            (!mstxRangeMessageUsageCountMap_[threadId].empty());
    }

    if (!isMstxEnabledMessageInit_) {
        SplitString(messageString, '|', mstxEnabledMessageList_);
        isMstxEnabledMessageInit_ = true;
    }

    return std::any_of(mstxEnabledMessageList_.begin(), mstxEnabledMessageList_.end(),
        [&](const std::string &oneSelectMessage) {
            return (mstxRangeMessageUsageCountMap_.find(threadId) != mstxRangeMessageUsageCountMap_.end()) &&
                (mstxRangeMessageUsageCountMap_[threadId].find(oneSelectMessage) != mstxRangeMessageUsageCountMap_[threadId].end());
        });
}

bool MsTx::IsMessageEnable(const std::string& msg)
{
    std::string messageString = std::string(ProfConfig::Instance().GetConfig().mstxProfConfig.mstxEnabledMessage);
    if (messageString.empty()) {
        return true;
    }
    std::lock_guard<std::mutex> lock(mstxAttrMutex_);
    if (!isMstxEnabledMessageInit_) {
        SplitString(messageString, '|', mstxEnabledMessageList_);
        isMstxEnabledMessageInit_ = true;
    }
    return mstxEnabledMessageList_.count(msg);
}

MstxAPI::MstxDomainRegistration* MsTx::MstxDomainCreateA(const std::string &domainName)
{
    std::lock_guard<std::mutex> lock(mstxAttrMutex_);
    if (!CheckInputStringValid(domainName, MAX_DOMAIN_LENGTH)) {
        ERROR_LOG("domainName is empty or contains more than %u characters or has illegal characters,"
            "legal characters is in [A-Za-z0-9_]", MAX_DOMAIN_LENGTH);
        return nullptr;
    }
    auto it = mstxName2DomainMap_.find(domainName);
    if (it == mstxName2DomainMap_.end()) {
        it = mstxName2DomainMap_.emplace(domainName, MakeUnique<MstxDomainRegistration>()).first;
    }
    mstxDomain2NameMap_[it->second.get()] = domainName;
    return it->second.get();
}

bool MsTx::IsDomainExist(MstxDomainRegistration *domain) const 
{
    if (domain == nullptr) {
         // globalDomain default is nullptr
        return true;
    }
    return mstxDomain2NameMap_.find(domain) != mstxDomain2NameMap_.end();
}

MstxMemHeap* MsTx::MstxMemHeapRegister(MstxDomainRegistration *domain, MstxMemHeapDesc const *desc)
{
    std::lock_guard<std::mutex> lock(mstxAttrMutex_);
    if (!IsDomainExist(domain)) {
        ERROR_LOG("domain does not exist, please see the api for creating a domain in the description document");
        return nullptr;
    }
    if (desc == nullptr || desc->typeSpecificDesc == nullptr) {
        ERROR_LOG("desc is illegal, please create a desc or replace it with an existing desc");
        return nullptr;
    }

    auto &memHeapVec = mstxDomainMemHeapMap_[domain];
    memHeapVec.push_back(MakeUnique<MstxMemHeap>());
    MstxMemHeapRangeDesc heapBasicDesc(*desc);
    mstxDomainMemHeapDescMap_[{domain, memHeapVec.back().get()}] = heapBasicDesc;
    return memHeapVec.back().get();
}

bool MsTx::MstxMemHeapUnregister(MstxDomainRegistration *domain, MstxMemHeap *heap, MstxMemHeapRangeDesc *desc)
{
    std::lock_guard<std::mutex> lock(mstxAttrMutex_);
    if (!IsDomainExist(domain)) {
        ERROR_LOG("domain does not exist, please see the api for creating a domain in the description document");
        return false;
    }
    if (heap == nullptr) {
        WARN_LOG("heap is nullptr, please create a heap or replace it with an existing heap");
        return false;
    }

    std::pair<MstxDomainRegistration *, MstxMemHeap *> key = {domain, heap};
    auto descIt = mstxDomainMemHeapDescMap_.find(key);
    if (descIt == mstxDomainMemHeapDescMap_.end()) {
        ERROR_LOG("heap is unregister, please run mstxMemHeapRegister to return this heap");
        return false;
    }

    auto heapIt = mstxDomainMemHeapMap_.find(domain);
    if (heapIt == mstxDomainMemHeapMap_.end()) {
        ERROR_LOG("domain cannot be found, please run mstxMemHeapRegister to register the domain");
        return false;
    }

    auto &memHeapVec = heapIt->second;
    for (auto it = memHeapVec.begin(); it != memHeapVec.end();) {
        if ((*it).get() == heap) {
            it = memHeapVec.erase(it);
        } else {
            ++it;
        }
    }

    *desc = descIt->second;
    mstxDomainMemHeapDescMap_.erase(descIt);
    return true;
}

MemRegionsBelongInfo MsTx::MstxMemRegionsRegister(MstxDomainRegistration *domain,
                                                  MstxMemRegionsRegisterBatch const *desc)
{
    std::lock_guard<std::mutex> lock(mstxAttrMutex_);
    if (!IsDomainExist(domain)) {
        ERROR_LOG("domain does not exist, please see the api for creating a domain in the description document");
        return {};
    }
    if (desc == nullptr || desc->regionDescArray == nullptr || desc->regionHandleArrayOut == nullptr) {
        ERROR_LOG("desc is illegal, please create a desc or replace it with an existing desc");
        return {};
    }
    std::pair<MstxDomainRegistration *, MstxMemHeap *> key = {domain, desc->heap};
    auto it = mstxDomainMemHeapDescMap_.find(key);
    if (desc->heap != nullptr && it == mstxDomainMemHeapDescMap_.end()) {
        ERROR_LOG("heap is unregister, please run mstxMemHeapRegister to return this heap");
        return {};
    }

    MemRegionsBelongInfo info{};
    info.success = true;
    if (it != mstxDomainMemHeapDescMap_.end()) {
        info.addr = reinterpret_cast<uint64_t>(it->second.rangeDesc.ptr);
        info.size = it->second.rangeDesc.size;
    }

    auto &regionsVec = mstxDomainRegionsMap_[domain];
    for (size_t i = 0; i < desc->regionCount; ++i) {
        regionsVec.push_back(MakeUnique<MstxMemRegion>());
        mstxRegionRangeDescMap_[regionsVec.back().get()] = static_cast<MstxMemVirtualRangeDesc const*>
            (desc->regionDescArray)[i];
        desc->regionHandleArrayOut[i] = regionsVec.back().get();
    }
    return info;
}

void MsTx::UpdateRegionsUnregisterInfo(MstxDomainRegistration const *domain, MstxMemRegionsUnregisterBatch const *desc,
    std::vector<MstxMemVirtualRangeDesc> &rangeDescVec, MstxMemRegion *targetHandle)
{
    (void)desc;
    auto regionIt = mstxRegionRangeDescMap_.find(targetHandle);
    if (regionIt != mstxRegionRangeDescMap_.end()) {
        rangeDescVec.push_back(regionIt->second);
        mstxRegionRangeDescMap_.erase(regionIt);
    }
    auto &memRegionVec = mstxDomainRegionsMap_[domain];
    for (auto it = memRegionVec.begin(); it != memRegionVec.end();) {
        auto itElement = (*it).get();
        if (itElement == targetHandle) {
            it = memRegionVec.erase(it);
        } else {
            ++it;
        }
    }
}

bool MsTx::MstxMemRegionsUnregister(MstxDomainRegistration *domain, MstxMemRegionsUnregisterBatch const *desc,
                                    std::vector<MstxMemVirtualRangeDesc> &rangeDescVec)
{
    std::lock_guard<std::mutex> lock(mstxAttrMutex_);
    if (!IsDomainExist(domain)) {
        ERROR_LOG("domain does not exist, please see the api for creating a domain in the description document");
        return false;
    }
    if (desc == nullptr || desc->refArray == nullptr) {
        ERROR_LOG("desc is illegal, please create a desc or replace it with an existing desc");
        return false;
    }
    if (mstxDomainRegionsMap_.find(domain) == mstxDomainRegionsMap_.end()) {
        ERROR_LOG("domain cannot be found, please run mstxMemRegionsRegister to register the domain");
        return false;
    }

    for (size_t i = 0; i < desc->refCount; ++i) {
        if (desc->refArray[i].refType == MstxMemRegionRefType::MSTX_MEM_REGION_REF_TYPE_HANDLE) {
            auto targetHandle = desc->refArray[i].handle;
            UpdateRegionsUnregisterInfo(domain, desc, rangeDescVec, targetHandle);
        }
    }
    return true;
}

bool MsTx::MstxMemPermissionsAssign(MstxAPI::MstxDomainRegistration *domain,
                                    MstxAPI::MstxMemPermissionsAssignBatch const *desc,
                                    std::vector<MstxAPI::MstxMemVirtualRangeDesc> &rangeDescVec)
{
    std::lock_guard<std::mutex> lock(mstxAttrMutex_);
    if (!IsDomainExist(domain)) {
        ERROR_LOG("domain does not exist, please see the api for creating a domain in the description document");
        return false;
    }
    if (desc == nullptr || desc->regionDescArray == nullptr) {
        ERROR_LOG("desc is illegal, please create a desc or replace it with an existing desc");
        return false;
    }
    if (mstxDomainRegionsMap_.find(domain) == mstxDomainRegionsMap_.end()) {
        ERROR_LOG("domain cannot be found, please run mstxMemRegionsRegister to register the domain");
        return false;
    }

    for (size_t i = 0; i < desc->regionCount; ++i) {
        if (desc->regionDescArray[i].region.refType == MstxMemRegionRefType::MSTX_MEM_REGION_REF_TYPE_HANDLE) {
            auto regionIt = mstxRegionRangeDescMap_.find(desc->regionDescArray[i].region.handle);
            if (regionIt == mstxRegionRangeDescMap_.end()) {
                return false;
            }
            rangeDescVec.push_back(regionIt->second);
        } else {
            ERROR_LOG("MSTX_MEM_REGION_REF_TYPE_POINTER is not supported yet. Use MSTX_MEM_REGION_REF_TYPE_HANDLE instead.");
            return false;
        }
    }
    return true;
}
