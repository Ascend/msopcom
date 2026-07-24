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

#include "DFXKernelLauncher.h"

#include <vector>
#include "utils/Environment.h"
#include "utils/FileSystem.h"
#include "utils/InjectLogger.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"

using namespace std;
namespace {
aclError CheckAclResult(aclError result, const string &apiName)
{
    if (result == ACL_SUCCESS) {
        return result;
    }
    WARN_LOG("aclrt API call %s() failed. error code: %d", apiName.c_str(), result);
    return result;
}
}
#define RETURN_IF_FAIL(result) \
    if ((result) != ACL_SUCCESS) { \
        return false; \
    }
void DFXKernelLauncher::Init(const std::string &kernelName, const std::string &kernelPath)
{
    std::unique_lock<std::mutex> lock(mtx_);
    if (kernelSet_.find(kernelName) != kernelSet_.end()) {
        return;
    }
    std::vector<char> binData;
    if (kernelPath.empty()) {
        DEBUG_LOG("Failed to get kernel %s path", kernelName.c_str());
        return;
    }
    void *binHandle = nullptr;
    void *funcHandle = nullptr;
    aclError ret = aclrtBinaryLoadFromFileImplOrigin(kernelPath.c_str(), nullptr, &binHandle);
    if (ret != ACL_SUCCESS) {
        binHandle = nullptr;
        WARN_LOG("Register DFX kernel binary failed, kernelPath is %s, ret is %d", kernelPath.c_str(),
                 static_cast<uint32_t>(ret));
        return;
    }
    DEBUG_LOG("Register DFX kernel binary success, kernelPath is %s", kernelPath.c_str());
    ret = aclrtBinaryGetFunctionImplOrigin(binHandle, kernelName.c_str(), &funcHandle);
    if (ret != ACL_SUCCESS) {
        if (binHandle != nullptr) {
            aclrtBinaryUnLoadImplOrigin(binHandle);
            binHandle = nullptr;
        }
        WARN_LOG("Register function failed, ret is %d", static_cast<uint32_t>(ret));
        return;
    }
    funcHandleMap_[kernelName] = funcHandle;
    kernelSet_.insert(kernelName);
    binHandleMap_[kernelName] = binHandle;
}


bool DFXKernelLauncher::CheckSupportSeries(const std::vector<ChipProductType> &chipSeries)
{
    std::string socVersion = DeviceContext::Local().GetSocVersion();
    auto chipType = GetProductTypeBySocVersion(socVersion);
    bool chipValid = false;
    for (const auto &series : chipSeries) {
        if (IsChipSeriesTypeValid(chipType, series)) {
            chipValid = true;
        }
    }
    if (!chipValid) {
        DEBUG_LOG("Failed to get kernel path because chip not support.");
    }
    return chipValid;
}

std::string DFXKernelLauncher::GetEmptyKernelPath()
{
    std::string kernelPath;
    std::vector<ChipProductType> chipSeries = {ChipProductType::ASCEND910B_SERIES,
                                               ChipProductType::ASCEND910_93_SERIES};
    if (!CheckSupportSeries(chipSeries)) {
        return "";
    }
    std::string msopprofPath = ProfConfig::Instance().GetMsopprofPath();
    if (msopprofPath.empty()) {
        DEBUG_LOG("Get msopt path failed when init DFXKernelLauncher.");
        return "";
    }
    kernelPath = JoinPath({msopprofPath, "lib64", "empty_kernel_dav-c220-vec.o"});
    return kernelPath;
}

std::string DFXKernelLauncher::GetL2CacheKernelPath()
{
    static const std::map<ChipProductType, std::string> chipToKernel {
        {ChipProductType::ASCEND910B_SERIES, "dfx_kernel_dav-c220-cube.o"},
        {ChipProductType::ASCEND910_93_SERIES, "dfx_kernel_dav-c220-cube.o"},
        {ChipProductType::ASCEND310P_SERIES, "dfx_kernel_dav-m200.o"},
        {ChipProductType::ASCEND950_SERIES, "dfx_kernel_dav-c310-cube.o"}
    };
    std::string socVersion = DeviceContext::Local().GetSocVersion();
    auto chipType = GetProductSeriesType(socVersion);
    auto objIter = chipToKernel.find(chipType);
    if (objIter == chipToKernel.end()) {
        DEBUG_LOG("Cannot find supported kernel when init DFXKernelLauncher.");
        return "";
    }
    std::string msopprofPath = ProfConfig::Instance().GetMsopprofPath();
    if (msopprofPath.empty()) {
        DEBUG_LOG("Get msopt path failed when init DFXKernelLauncher.");
        return "";
    }
    std::string kernelPath = JoinPath({msopprofPath, "lib64", objIter->second});
    return kernelPath;
}


bool DFXKernelLauncher::CallClearL2Cache(uint32_t blockDim, void *stream,  std::vector<void *> &kernelArgs)
{
    std::string kernelName = "ClearL2Cache";
    static std::string l2CachePath = []() -> std::string { return GetL2CacheKernelPath(); }();
    if (!CallKernel(kernelName, l2CachePath, blockDim, stream, kernelArgs)) {
        WARN_LOG("Failed to clear L2cache by operator");
        return false;
    }
    return true;
}

bool DFXKernelLauncher::CallEmptyKernel(void *stream)
{
    std::string kernelName = "EmptyKernel";
    std::vector<void *> inputArgs = {};
    uint32_t blockDim = 1;
    static std::string emptyPath = []() -> std::string { return GetEmptyKernelPath(); }();
    if (!CallKernel(kernelName, emptyPath, blockDim, stream, inputArgs)) {
        WARN_LOG("Failed to call empty kernel for L2cache");
        return false;
    }
    return true;
}


bool DFXKernelLauncher::CallKernel(const std::string &kernelName, const std::string &kernelPath, uint32_t blockDim, void *stream, std::vector<void *> &kernelArgs)
{
    Init(kernelName, kernelPath);
    std::unique_lock<std::mutex> lock(mtx_);
    if (kernelSet_.find(kernelName) == kernelSet_.end()) {
        DEBUG_LOG("Can not find %s in kernelMap.", kernelName.c_str());
        return false;
    }
    void *funcHandle = funcHandleMap_[kernelName];
    size_t memSize;
    aclError ret = CheckAclResult(aclrtKernelArgsGetHandleMemSizeImplOrigin(funcHandle, &memSize),
                                  "aclrtKernelArgsGetHandleMemSizeImpl");
    RETURN_IF_FAIL(ret);
    aclrtArgsHandle argsHandle;
    ret = CheckAclResult(aclrtMallocHostImplOrigin(&argsHandle, memSize), "aclrtMallocHostImpl");
    RETURN_IF_FAIL(ret);
    shared_ptr<void> argsHandleDefer(nullptr, [&argsHandle](std::nullptr_t&) {
        aclrtFreeHostImplOrigin(argsHandle);
    });
    size_t actualArgsSize;
    ret = CheckAclResult(aclrtKernelArgsGetMemSizeImplOrigin(
        funcHandle, kernelArgs.size() * 8, &actualArgsSize), "aclrtKernelArgsGetMemSizeImpl");
    RETURN_IF_FAIL(ret);
    void *userHostMem;
    ret = CheckAclResult(aclrtMallocHostImplOrigin(&userHostMem, actualArgsSize), "aclrtMallocHostImpl");
    RETURN_IF_FAIL(ret);
    shared_ptr<void> userHostMemDefer(nullptr, [&userHostMem](std::nullptr_t&) {
        aclrtFreeHostImplOrigin(userHostMem);
    });
    ret = CheckAclResult(aclrtKernelArgsInitByUserMemImplOrigin(
        funcHandle, argsHandle, userHostMem, actualArgsSize), "aclrtKernelArgsInitByUserMemImpl");
    RETURN_IF_FAIL(ret);
    aclrtParamHandle paramHandle {nullptr};
    aclrtKernelArgsAppendImplOrigin(argsHandle, kernelArgs.data(), kernelArgs.size() * 8, &paramHandle);
    aclrtKernelArgsFinalizeImplOrigin(argsHandle);
    ret = CheckAclResult(aclrtLaunchKernelWithConfigImplOrigin(
        funcHandle, blockDim, stream, nullptr, argsHandle, nullptr), "aclrtLaunchKernelWithConfigImpl");
    RETURN_IF_FAIL(ret);
    ret = CheckAclResult(aclrtSynchronizeStreamImplOrigin(stream), "aclrtSynchronizeStreamImpl");
    RETURN_IF_FAIL(ret);
    DEBUG_LOG("Success run kernel %s.", kernelName.c_str());
    return true;
}

DFXKernelLauncher::~DFXKernelLauncher()
{
    for (auto &handle : binHandleMap_) {
        if (handle.second != nullptr) {
            aclrtBinaryUnLoadImplOrigin(handle.second);
            handle.second = nullptr;
        }
    }
    for (auto &handle : funcHandleMap_) {
        if (handle.second != nullptr) {
            handle.second = nullptr;
        }
    }
}
