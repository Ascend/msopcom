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


#include "KernelRunner.h"

#include <algorithm>
#include <cmath>
#include <map>
#include "utils/FileSystem.h"
#include "utils/InjectLogger.h"

using namespace std;

#define CHECK_RT_RESULT(result) if (!(result)) {return false;}

bool KernelRunner::Run(const KernelConfig& kernelConfig)
{
    // set device
    if (!rtAPI_.CheckRtResult(rtAPI_.RtSetDevice(kernelConfig.deviceID), "rtSetDevice")) {
        return false;
    }
    needResetDevice_ = true;
    deviceID_ = kernelConfig.deviceID;

    // create stream
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(rtAPI_.RtStreamCreate(&rtStream_, 0), "rtStreamCreate"))
    needDestroyStream_ = true;

    // register kernel
    size_t fileSize = GetFileSize(kernelConfig.kernelBinaryPath);
    std::vector<char> bin;
    if (ReadBinary(kernelConfig.kernelBinaryPath, bin) == 0) {
        ERROR_LOG("read op kernel file failed.");
        return false;
    }
    if (!RegisterKernel(kernelConfig, bin, fileSize)) {
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
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(rtAPI_.RtStreamSynchronize(rtStream_), "rtStreamSynchronize"))
    if (!SaveOutputs(kernelConfig.outputDir)) {
        return false;
    }
    return true;
}

bool KernelRunner::LaunchKernel(KernelConfig const &kernelConfig)
{
    if (kernelConfig.hasTilingKey) {
        const size_t argsByteSize = kernelArgs_.size() * sizeof(void*);
        std::vector<char> args(argsByteSize + tilingDataSize_, 0);
        const char *src = reinterpret_cast<const char *>(kernelArgs_.data());
        std::copy_n(src, argsByteSize, args.begin());
        src = static_cast<char *>(tilingData_);
        std::copy_n(src, tilingDataSize_, args.begin() + argsByteSize);

        rtArgsEx_t argsEx {};
        argsEx.args = args.data();
        argsEx.hostInputInfoPtr = nullptr;
        argsEx.argsSize = args.size();
        argsEx.tilingAddrOffset = tilingAddrOffset_;
        argsEx.tilingDataOffset = kernelArgs_.size() * sizeof(void*);
        argsEx.hasTiling = 1;
        argsEx.isNoNeedH2DCopy = 0;
        CHECK_RT_RESULT(rtAPI_.CheckRtResult(
            rtAPI_.RtKernelLaunchWithHandleV2(binHandle_, kernelConfig.tilingKey,
                                              kernelConfig.blockDim, &argsEx, nullptr,
                                              rtStream_, nullptr),
            "rtKernelLaunchWithHandleV2"));
        return true;
    } else {
        CHECK_RT_RESULT(rtAPI_.CheckRtResult(
            rtAPI_.RtKernelLaunch(registerStub_.c_str(), kernelConfig.blockDim,
                                  kernelArgs_.data(),
                                  uint32_t(kernelArgs_.size() * sizeof(void*)), rtStream_),
            "rtKernelLaunch"));
        return true;
    }
}

bool KernelRunner::RegisterKernel(const KernelConfig& kernelConfig, const std::vector<char> &data, uint64_t fileSize)
{
    rtDevBinary_t deviceBinary {};
    deviceBinary.version = 0;
    deviceBinary.data = data.data();
    deviceBinary.magic = static_cast<uint32_t>(rtAPI_.GetMagic(kernelConfig.magic));
    deviceBinary.length = fileSize;
    if (kernelConfig.hasTilingKey) {
        CHECK_RT_RESULT(rtAPI_.CheckRtResult(
            rtAPI_.RtRegisterAllKernel(&deviceBinary, &binHandle_),
            "rtRegisterAllKernel"))
    } else {
        CHECK_RT_RESULT(rtAPI_.CheckRtResult(
            rtAPI_.RtDevBinaryRegister(&deviceBinary, &binHandle_),
            "rtDevBinaryRegister"))
        registerStub_ = kernelConfig.kernelName;
        CHECK_RT_RESULT(rtAPI_.CheckRtResult(
            rtAPI_.RtFunctionRegister(binHandle_, registerStub_.c_str(),
                                      registerStub_.c_str(),
                                      registerStub_.c_str(), 0),
            "rtFunctionRegister"))
    }
    needUnRegisterDevBinary_ = true;
    return true;
}

bool KernelRunner::InitInput(const Param &in)
{
    if (!in.isRequired) {
        kernelArgs_.emplace_back(nullptr);
        return true;
    }
    size_t dataSize = in.dataSize;
    auto memorySize = static_cast<size_t>(ceil(static_cast<double>(dataSize) / 32) * 32 + 32);
    void *hostInputPtr;
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(rtAPI_.RtMallocHost(&hostInputPtr, dataSize),
                                         "rtMallocHost"))
    hostInputPtrs_.emplace_back(hostInputPtr);
    if (!ReadFile(in.dataPath, (uint8_t *) hostInputPtr, dataSize, true)) {
        return false;
    }
    void *deviceInputPtr;
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(
        rtAPI_.RtMalloc(&deviceInputPtr, memorySize, rtMemType_t::RT_MEMORY_DDR_NC),
        "rtMalloc"))
    devInputPtrs_.emplace_back(deviceInputPtr);
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(
        rtAPI_.RtMemcpy(deviceInputPtr, dataSize, hostInputPtr, dataSize,
        rtMemcpyKind_t::RT_MEMCPY_HOST_TO_DEVICE), "rtMemcpy"))
    kernelArgs_.emplace_back(deviceInputPtr);
    return true;
}

bool KernelRunner::InitOutput(const Param &out)
{
    size_t dataSize = out.dataSize;
    void *hostOutputPtr;
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(
        rtAPI_.RtMallocHost(&hostOutputPtr, dataSize), "rtMallocHost"))
    hostOutputPtrs_.emplace_back(hostOutputPtr);
    void *deviceOutputPtr;
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(
        rtAPI_.RtMalloc(&deviceOutputPtr, dataSize, rtMemType_t::RT_MEMORY_DDR_NC), "rtMalloc"))
    devOutputPtrs_.emplace_back(deviceOutputPtr);
    kernelArgs_.emplace_back(deviceOutputPtr);
    return true;
}

bool KernelRunner::InitWorkspace(const Param &workspace)
{
    size_t dataSize = workspace.dataSize + 16 * 1024 * 1024;
    void *deviceInputPtr;
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(
        rtAPI_.RtMalloc(&deviceInputPtr, dataSize, rtMemType_t::RT_MEMORY_DDR_NC), "rtMalloc"))
    devInputPtrs_.emplace_back(deviceInputPtr);
    kernelArgs_.emplace_back(deviceInputPtr);
    return true;
}

bool KernelRunner::InitFftsAddr()
{
    DEBUG_LOG("Set dynamic ffts args");
    uint64_t addr;
    uint32_t addrLen;
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(rtAPI_.RtGetC2cCtrlAddr(&addr, &addrLen), "rtGetC2cCtrlAddr"))
    kernelArgs_.emplace_back(reinterpret_cast<void *>(addr));
    return true;
}

bool KernelRunner::InitFftsAddr(const Param &fftsAddr)
{
    DEBUG_LOG("Set static ffts args");
    uint64_t addr;
    uint32_t addrLen;
    uint32_t dataSize = fftsAddr.dataSize;
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(rtAPI_.RtGetC2cCtrlAddr(&addr, &addrLen),
                                         "rtGetC2cCtrlAddr"))
    void *fftsHost;
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(rtAPI_.RtMallocHost(&fftsHost, dataSize),
                                         "rtMallocHost"))
    void *fftsDevice;
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(
        rtAPI_.RtMalloc(&fftsDevice, dataSize, rtMemType_t::RT_MEMORY_DDR_NC), "rtMalloc"))
    *((uint64_t *)fftsHost) = addr;
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(
        rtAPI_.RtMemcpy(fftsDevice, dataSize, fftsHost, dataSize, rtMemcpyKind_t::RT_MEMCPY_HOST_TO_DEVICE),
        "rtMemcpy"))
    kernelArgs_.emplace_back(fftsDevice);
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(rtAPI_.RtFree(fftsHost), "rtFree"))
    return true;
}
bool KernelRunner::InitTiling(const Param &tiling)
{
    size_t dataSize = tiling.dataSize;
    auto memorySize = static_cast<size_t>(ceil(static_cast<double>(dataSize) / 32) * 32 + 32);
    void *hostInputPtr;
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(rtAPI_.RtMallocHost(&hostInputPtr, dataSize),
                                         "rtMallocHost"))
    hostInputPtrs_.emplace_back(hostInputPtr);
    if (!ReadFile(tiling.dataPath, (uint8_t *) hostInputPtr, dataSize, true)) {
        return false;
    }
    void *deviceInputPtr;
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(rtAPI_.RtMalloc(&deviceInputPtr, memorySize, rtMemType_t::RT_MEMORY_DDR_NC),
                                         "rtMalloc"))
    devInputPtrs_.emplace_back(deviceInputPtr);
    CHECK_RT_RESULT(rtAPI_.CheckRtResult(rtAPI_.RtMemcpy(deviceInputPtr,
                                                         dataSize,
                                                         hostInputPtr,
                                                         dataSize,
                                                         rtMemcpyKind_t::RT_MEMCPY_HOST_TO_DEVICE),
                                         "rtMemcpy"))
    tilingAddrOffset_ = kernelArgs_.size() * sizeof(void*);
    tilingDataSize_ = dataSize;
    tilingData_ = hostInputPtr;
    kernelArgs_.emplace_back(deviceInputPtr);
    return true;
}

bool KernelRunner::SaveOutputs(const string &outputDir)
{
    size_t outputSize = min({hostOutputPtrs_.size(), outputs_.size(), devOutputPtrs_.size()});
    for (size_t i = 0; i < outputSize; i++) {
        auto out = outputs_[i];
        size_t dataSize = out.dataSize;
        CHECK_RT_RESULT(rtAPI_.CheckRtResult(rtAPI_.RtMemcpy(hostOutputPtrs_[i],
                                                             dataSize,
                                                             devOutputPtrs_[i],
                                                             dataSize,
                                                             rtMemcpyKind_t::RT_MEMCPY_DEVICE_TO_HOST),
                                             "rtMemcpy"))
        if (!MkdirRecusively(outputDir)) {
            WARN_LOG("Failed to create directory %s", outputDir.c_str());
            return false;
        }
        string filePath = outputDir + "/" + out.name + ".bin";
        if (WriteBinary(filePath, static_cast<const char *>(hostOutputPtrs_[i]), dataSize) != dataSize) {
            return false;
        }
    }
    return true;
}

bool KernelRunner::InitData(const KernelConfig& kernelConfig)
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
        } else if (param.type == "fftsAddr") {
            if ((kernelConfig.hasTilingKey && !InitFftsAddr()) ||
                (!kernelConfig.hasTilingKey && !InitFftsAddr(param))) {
                return false;
            }
        }
    }
    return true;
}

KernelRunner::~KernelRunner()
{
    if (needUnRegisterDevBinary_) {
        rtAPI_.CheckRtResult(rtAPI_.RtDevBinaryUnRegister(binHandle_), "rtDevBinaryUnRegister");
    }
    if (needDestroyStream_) {
        rtAPI_.CheckRtResult(rtAPI_.RtStreamDestroy(rtStream_), "rtStreamDestroy");
    }
    for (auto dev: devInputPtrs_) {
        if (dev != nullptr) {
            rtAPI_.CheckRtResult(rtAPI_.RtFree(dev), "rtFree");
        }
    }
    for (auto host: hostInputPtrs_) {
        if (host != nullptr) {
            rtAPI_.CheckRtResult(rtAPI_.RtFreeHost(host), "rtFreeHost");
        }
    }
    for (auto dev: devOutputPtrs_) {
        if (dev != nullptr) {
            rtAPI_.CheckRtResult(rtAPI_.RtFree(dev), "rtFree");
        }
    }
    for (auto host: hostOutputPtrs_) {
        if (host != nullptr) {
            rtAPI_.CheckRtResult(rtAPI_.RtFreeHost(host), "rtFreeHost");
        }
    }
    if (needResetDevice_) {
        rtAPI_.CheckRtResult(rtAPI_.RtDeviceReset(deviceID_), "rtDeviceReset");
    }
}
