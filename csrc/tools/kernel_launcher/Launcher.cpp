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


#include "Launcher.h"

#include <algorithm>
#include <cmath>
#include <map>
#include "utils/FileSystem.h"
#include "utils/Path.h"
namespace {
bool CheckResult(aclError result, const std::string &apiName)
{
    if (result == 0) {
        DEBUG_LOG("Aclrt API call %s() success.", apiName.c_str());
        return true;
    }

    ERROR_LOG("Aclrt API call %s() failed. error code: %d", apiName.c_str(), result);
    return false;
}
}
#define ACL_CHECK_MESSAGE_AND_RETURN(ret, apiName) \
do { \
    bool success = CheckResult(ret, apiName); \
    if (!success) { \
        return false; \
    } \
} while (0)

void Launcher::GenJson(const KernelConfig& kernelConfig)
{
    std::string jsonFilePath;
    const auto dotPos = kernelConfig.kernelBinaryPath.find_last_of('.');
    if (dotPos == std::string::npos) {
        WARN_LOG("Wrong object file name");
        return;
    }
    jsonFilePath = kernelConfig.kernelBinaryPath.substr(0, dotPos) + ".json";
    if (IsExist(jsonFilePath)) {
        return;
    }
    nlohmann::json jsonData;
    jsonData["intercoreSync"] = 0;
    jsonData["magic"] = kernelConfig.magic;
    for (const auto& param : kernelConfig.params) {
        if (param.name == "fftsAddr") {
            jsonData["intercoreSync"] = 1;
            break;
        }
    }
    std::string content = jsonData.dump();
    if (!WriteStringToFile(jsonFilePath, content)) {
        WARN_LOG("Kernel context save failed, json write failed");
        return;
    } else {
        INFO_LOG("Kernel context save success, json write success, %s", jsonFilePath.c_str());
    }
    needDeleteJson_ = true;
    jsonFilePath_ = jsonFilePath;
}

bool Launcher::Run(const KernelConfig& kernelConfig)
{
    GenJson(kernelConfig);
    // set device
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtSetDevice(kernelConfig.deviceID), "aclrtSetDevice");
    needResetDevice_ = true;
    deviceID_ = kernelConfig.deviceID;

    // create stream
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtCreateStream(&stream_), "aclrtCreateStream");
    needDestroyStream_ = true;
    // register kernel
    if (!RegisterKernel(kernelConfig, kernelConfig.kernelBinaryPath)) {
        return false;
    }

    // read file, operate memory of inputs, outputs, tiling data
    if (!InitData(kernelConfig)) {
        return false;
    }
    // execute kernel function and get output result
    if (!LaunchKernel(kernelConfig)) {
        return false;
    }
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtSynchronizeStream(stream_), "aclrtSynchronizeStream");
    if (!SaveOutputs(kernelConfig.outputDir)) {
        return false;
    }
    return true;
}

bool Launcher::LaunchKernel(KernelConfig const &kernelConfig)
{
    aclrtParamHandle paramHandle;
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtKernelArgsAppend(
            argsHandle_, kernelArgs_.data(), kernelArgs_.size() * 8, &paramHandle), "aclrtKernelArgsAppend");
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtKernelArgsFinalize(argsHandle_), "aclrtKernelArgsFinalize");
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtLaunchKernelWithConfig(
            funcHandle_, kernelConfig.blockDim, stream_, nullptr, argsHandle_, nullptr), "aclrtLaunchKernelWithConfig");
    return true;
}

bool Launcher::RegisterKernel(const KernelConfig& kernelConfig, std::string const &filename)
{
    // 1 magic，ffts不用配置，ffts會通過meta段（aclnn）或者json文件获取
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtBinaryLoadFromFile(filename.c_str(), nullptr, &binHandle_),
                                 "aclrtBinaryLoadFromFile");
    needUnRegisterDevBinary_ = true;
    if (kernelConfig.hasTilingKey) {
        ACL_CHECK_MESSAGE_AND_RETURN(aclrtBinaryGetFunctionByEntry(binHandle_, kernelConfig.tilingKey, &funcHandle_),
                                     "aclrtBinaryGetFunctionByEntry");
    } else {
        // 2 kernelName 不能加_mix_aic/_mix_aiv
        ACL_CHECK_MESSAGE_AND_RETURN(aclrtBinaryGetFunction(binHandle_, kernelConfig.kernelName.c_str(), &funcHandle_),
                                     "aclrtBinaryGetFunction");
    }
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtKernelArgsInit(funcHandle_, &argsHandle_), "aclrtKernelArgsInit");
    return true;
}

bool Launcher::InitInput(const Param &in)
{
    if (!in.isRequired) {
        kernelArgs_.emplace_back(nullptr);
        DEBUG_LOG("input emplace nullptr");
        return true;
    }
    size_t dataSize = in.dataSize;
    void *hostInputPtr;
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtMallocHost(&hostInputPtr, dataSize), "aclrtMallocHost");
    hostInputPtrs_.emplace_back(hostInputPtr);
    if (!ReadFile(in.dataPath, (uint8_t *) hostInputPtr, dataSize, true)) {
        return false;
    }
    void *deviceInputPtr;
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtMalloc(&deviceInputPtr, dataSize, ACL_MEM_MALLOC_HUGE_FIRST),
                                 "aclrtMalloc");
    devInputPtrs_.emplace_back(deviceInputPtr);
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtMemcpy(deviceInputPtr, dataSize, hostInputPtr, dataSize,
                                             aclrtMemcpyKind::ACL_MEMCPY_HOST_TO_DEVICE), "aclrtMemcpy");
    kernelArgs_.emplace_back(deviceInputPtr);
    return true;
}

bool Launcher::InitOutput(const Param &out)
{
    size_t dataSize = out.dataSize;
    void *hostOutputPtr;
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtMallocHost(&hostOutputPtr, dataSize), "aclrtMallocHost");
    hostOutputPtrs_.emplace_back(hostOutputPtr);
    void *deviceOutputPtr;
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtMalloc(&deviceOutputPtr, dataSize, ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc");
    devOutputPtrs_.emplace_back(deviceOutputPtr);
    kernelArgs_.emplace_back(deviceOutputPtr);
    return true;
}

bool Launcher::InitWorkspace(const Param &workspace)
{
    size_t dataSize = workspace.dataSize + 16 * 1024 * 1024;
    void *deviceInputPtr;
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtMalloc(&deviceInputPtr, dataSize, ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc");
    devInputPtrs_.emplace_back(deviceInputPtr);
    kernelArgs_.emplace_back(deviceInputPtr);
    return true;
}

bool Launcher::InitTiling(const Param &tiling)
{
    size_t dataSize = tiling.dataSize;
    auto memorySize = static_cast<size_t>(ceil(static_cast<double>(dataSize) / 32) * 32 + 32);
    void *hostInputPtr;
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtMallocHost(&hostInputPtr, dataSize), "aclrtMallocHost");
    hostInputPtrs_.emplace_back(hostInputPtr);
    if (!ReadFile(tiling.dataPath, (uint8_t *) hostInputPtr, dataSize, true)) {
        return false;
    }
    void *deviceInputPtr;
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtMalloc(&deviceInputPtr, memorySize, ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc");
    devInputPtrs_.emplace_back(deviceInputPtr);
    ACL_CHECK_MESSAGE_AND_RETURN(aclrtMemcpy(deviceInputPtr, dataSize, hostInputPtr, dataSize,
                                             aclrtMemcpyKind::ACL_MEMCPY_HOST_TO_DEVICE), "aclrtMemcpy");
    kernelArgs_.emplace_back(deviceInputPtr);
    return true;
}

bool Launcher::SaveOutputs(const std::string &outputDir)
{
    if (outputDir.empty()) {
        DEBUG_LOG("Launcher output is empty");
        return true;
    }
    if (!MkdirRecusively(outputDir)) {
        WARN_LOG("Failed to create directory %s", outputDir.c_str());
        return false;
    }

    const std::string resolveOutputDir = Realpath(outputDir);
    if (resolveOutputDir.empty()) {
        ERROR_LOG("Failed to resolve output directory %s", outputDir.c_str());
        return false;
    }

    size_t outputSize = std::min({hostOutputPtrs_.size(), outputs_.size(), devOutputPtrs_.size()});
    for (size_t i = 0; i < outputSize; i++) {
        const auto &out = outputs_[i];
        std::string filePath = resolveOutputDir + "/" + out.name + ".bin";

        std::string canonicalPath = Path(filePath).PathCanonicalize().ToString();
        if (canonicalPath.rfind(resolveOutputDir, 0) != 0 ||
            (canonicalPath.length() > resolveOutputDir.length() && canonicalPath[resolveOutputDir.length()] != '/')) {
            ERROR_LOG("Output path %s escapes output directory %s", canonicalPath.c_str(), resolveOutputDir.c_str());
            return false;
        }

        size_t dataSize = out.dataSize;
        ACL_CHECK_MESSAGE_AND_RETURN(aclrtMemcpy(hostOutputPtrs_[i], dataSize, devOutputPtrs_[i], dataSize,
                                         aclrtMemcpyKind::ACL_MEMCPY_DEVICE_TO_HOST),
            "aclrtMemcpy");

        if (WriteBinary(filePath, static_cast<const char *>(hostOutputPtrs_[i]), dataSize) != dataSize) {
            return false;
        }
    }
    return true;
}

bool Launcher::InitData(const KernelConfig& kernelConfig)
{
    auto params = kernelConfig.params;
    for (const auto& param: params) {
        if (param.type == "input" && !InitInput(param)) {
            return false;
        } else if (param.type == "output") {
            if (!InitOutput(param)) {
                return false;
            }
            outputs_.emplace_back(param);
        } else if (param.type == "tiling" && !InitTiling(param)) {
            return false;
        } else if (param.type == "workspace" && !InitWorkspace(param)) {
            return false;
        }
    }
    return true;
}

Launcher::~Launcher()
{
    if (needUnRegisterDevBinary_) {
        CheckResult(aclrtBinaryUnLoad(binHandle_), "aclrtBinaryUnLoad");
    }
    if (needDestroyStream_) {
        CheckResult(aclrtDestroyStream(stream_), "aclrtDestroyStream");
    }
    for (auto dev: devInputPtrs_) {
        if (dev != nullptr) {
            CheckResult(aclrtFree(dev), "aclrtFree");
        }
    }
    for (auto host: hostInputPtrs_) {
        if (host != nullptr) {
            CheckResult(aclrtFreeHost(host), "aclrtFreeHost");
        }
    }
    for (auto dev: devOutputPtrs_) {
        if (dev != nullptr) {
            CheckResult(aclrtFree(dev), "aclrtFree");
        }
    }
    for (auto host: hostOutputPtrs_) {
        if (host != nullptr) {
            CheckResult(aclrtFreeHost(host), "aclrtFreeHost");
        }
    }
    if (needResetDevice_) {
        CheckResult(aclrtResetDevice(deviceID_), "aclrtResetDevice");
    }
    if (needDeleteJson_) {
        RemoveAll(jsonFilePath_);
    }
}
