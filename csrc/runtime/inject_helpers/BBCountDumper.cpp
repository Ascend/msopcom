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

#include "BBCountDumper.h"

#include <fstream>
#include "acl.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "utils/FileSystem.h"
#include "core/PlatformConfig.h"
#include "runtime/inject_helpers/DBITask.h"

using namespace std;

std::string BBCountDumper::GetOutputDir()
{
    if (outputDir_.empty()) {
        string msg = "$GetOutputPath;";
        LocalProcess::GetInstance().Notify(msg);
        uint32_t maxReadTime = 1000;
        if (LocalProcess::GetInstance().Wait(msg, maxReadTime) > 0) {
            uint32_t minLength = 2;
            if (msg.length() > minLength && msg[0] == '$' && msg.back() == ';') {
                uint64_t length = msg.length() - 2;
                outputDir_ = msg.substr(1, length);
            }
        }
    }
    return outputDir_;
}

string BBCountDumper::GetOutFileName(DataType type, uint64_t regId) const
{
    switch (type) {
        case DataType::KERNEL:
            return "kernel" + std::to_string(regId) + ".o";
        case DataType::STUBKERNEL:
            return "kernel" + std::to_string(regId) + "Stub.o";
        case DataType::BLOCKMAP:
            return "kernel" + std::to_string(regId) + "Stub.o.bbbmap";
        case DataType::EXTRAMEM:
            return "kernel" + std::to_string(regId) + ".extra";
        case DataType::INVALID:
        default:
            return "";
    }
}

uint64_t BBCountDumper::GetMemSize(uint64_t regId, const std::string& outputPath)
{
    uint64_t bbCountNum = 0;
    std::string line;
    std::string bbbIdStr = "BBB id";
    uint64_t subKernelID = lastLaunchId_[regId];
    std::string outPath = outputPath.empty() ? GetOutputDir() : outputPath;
    std::string filePath = JoinPath({outPath,
                                     GetOutFileName(DataType::BLOCKMAP, regId) + "." + to_string(subKernelID)});
    if (!IsPathExists(filePath) || !CheckInputFileValid(filePath, "bin")) {
        WARN_LOG("check file path %s failed", filePath.c_str());
        return 0;
    }
    constexpr uint64_t MAX_FILE_SIZE = 1ULL * 1024 * 1024 * 1024;
    if (GetFileSize(filePath) <= MAX_FILE_SIZE) {
        std::ifstream file(filePath);
        while (std::getline(file, line)) {
            if (line.find(bbbIdStr) != std::string::npos) {
                bbCountNum++;
            }
        }
        file.close();
    } else {
        WARN_LOG("file size is over 1G, file size: %zu", GetFileSize(filePath));
        return 0;
    }

    constexpr int uint = 4; // one block use 4 byte
    constexpr int align = 256; // 256 byte align
    uint64_t coreUnitSize = ((bbCountNum * uint) + align - 1)  / align * align;
    constexpr uint64_t MAX_CORE_UNIT_SIZE = 50ULL * 1024 * 1024;
    if (coreUnitSize > MAX_CORE_UNIT_SIZE) {
        WARN_LOG("coreUnitSize is over 50M, use 50M as coreUnitSize. coreUnitSize: %lu", coreUnitSize);
        coreUnitSize = MAX_CORE_UNIT_SIZE;
    }
    return coreUnitSize * C220_MIX_SUB_BLOCKDIM * MAX_BLOCKDIM_NUMS;
}

std::string BBCountDumper::GenExtraAndReturnName(const std::string &path, uint64_t regId,
                                                 uint64_t memSize, uint8_t *memInfo)
{
    if (memSize == 0) {
        return "";
    }
    auto *memInfoHost = new uint8_t[memSize]; // NOLINT(cppcoreguidelines-owning-memory)
    aclError error = aclrtMemcpyImplOrigin(memInfoHost, memSize, memInfo, memSize, ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("dump basic block data rtMemcpy memInfo error: %d", error);
        delete[] memInfoHost; // NOLINT(cppcoreguidelines-owning-memory)
        memInfoHost = nullptr;
        return "";
    }
    uint64_t lastLaunchId = lastLaunchId_[regId];
    std::string extraMemFileName = GetOutFileName(DataType::EXTRAMEM, regId) + "." + to_string(lastLaunchId);
    std::string extraMemPath = JoinPath({path, extraMemFileName});
    bool dumpResult = WriteBinary(extraMemPath, reinterpret_cast<const char*>(memInfoHost), memSize);
    if (!dumpResult) {
        ERROR_LOG("rtKernelLaunch write memInfo file failed.");
    }
    delete[] memInfoHost; // NOLINT(cppcoreguidelines-owning-memory)
    memInfoHost = nullptr;
    return extraMemFileName; // NOLINT(cppcoreguidelines-owning-memory)
}

bool BBCountDumper::DumpBBData(uint64_t regId, uint64_t memSize, uint8_t *memInfo)
{
    if (memSize == 0) {
        return false;
    }
    DEBUG_LOG("start to dump BBData for regId=%lu memSize=%lu", regId, memSize);
    auto *memInfoHost = new uint8_t[memSize]; // NOLINT(cppcoreguidelines-owning-memory)
    aclError error = aclrtMemcpyImplOrigin(memInfoHost, memSize, memInfo, memSize, ACL_MEMCPY_DEVICE_TO_HOST);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("dump basic block data rtMemcpy memInfo error: %d", error);
        delete[] memInfoHost; // NOLINT(cppcoreguidelines-owning-memory)
        memInfoHost = nullptr;
        return false;
    }
    uint64_t lastLaunchId = lastLaunchId_[regId];
    std::string extraMemPath = JoinPath({GetOutputDir(),
                                         GetOutFileName(DataType::EXTRAMEM, regId) + "." + to_string(lastLaunchId)});
    bool dumpResult = WriteBinary(extraMemPath, reinterpret_cast<const char*>(memInfoHost), memSize);
    if (!dumpResult) {
        ERROR_LOG("rtKernelLaunch write memInfo file failed.");
    }

    delete[] memInfoHost; // NOLINT(cppcoreguidelines-owning-memory)
    memInfoHost = nullptr;
    return dumpResult;
}

void BBCountDumper::InitDBITaskConfig(uint64_t regId, const std::string &outputPath) const
{
    DBITaskConfig::Instance().Init(type_, matcher_, outputPath);
    DBITaskConfig::Instance().KeepTaskOutputs();
    DBITaskConfig::Instance().oldKernelName_ = GetOutFileName(DataType::KERNEL, regId);
    DBITaskConfig::Instance().newKernelName_ = GetOutFileName(DataType::STUBKERNEL, regId);
}

bool BBCountDumper::Replace(const StubFunc **stubFunc, uint64_t launchId, const std::string &outputPath)
{
    if (!enabled_) {
        ERROR_LOG("Replace invoked before Init");
        return false;
    }
    uint64_t regId = KernelContext::Instance().GetRegisterId(launchId);
    InitDBITaskConfig(regId, outputPath);
    if (!RunDBITask(stubFunc)) {
        return false;
    }
    SaveBlockMapFile(launchId, regId, outputPath);
    return true;
}

bool BBCountDumper::Replace(void **hdl, const uint64_t tilingKey, uint64_t launchId, const std::string &outputPath)
{
    if (!enabled_) {
        ERROR_LOG("Replace invoked before Init");
        return false;
    }
    uint64_t regId = KernelContext::Instance().GetRegisterId(launchId);
    InitDBITaskConfig(regId, outputPath);
    if (!RunDBITask(hdl, tilingKey)) {
        return false;
    }
    SaveBlockMapFile(launchId, regId, outputPath);
    return true;
}

FuncContextSP BBCountDumper::Replace(const LaunchContextSP &launchCtx, const std::string &outputPath)
{
    if (!enabled_) {
        ERROR_LOG("Replace invoked before Init");
        return nullptr;
    }
    uint64_t regId = launchCtx->GetFuncContext()->GetRegisterContext()->GetRegisterId();
    InitDBITaskConfig(regId, outputPath);
    auto funcCtx = RunDBITask(launchCtx);
    if (funcCtx == nullptr) {
        ERROR_LOG("BB count run task failed");
        return nullptr;
    }
    SaveBlockMapFile(launchCtx->GetLaunchId(), regId, outputPath);
    return funcCtx;
}

void BBCountDumper::SaveBlockMapFile(uint64_t launchId, uint64_t regId, const std::string &outputPath)
{
    string outDir = outputPath.empty() ?  GetOutputDir() : outputPath;
    string dbiDir = DBITaskConfig::Instance().GetOutputDir(launchId);
    std::string baseBBMapPath = JoinPath({dbiDir, GetOutFileName(DataType::BLOCKMAP, regId)});
    std::string oldKernelPath = JoinPath({dbiDir, GetOutFileName(DataType::KERNEL, regId)});
    if (!IsPathExists(baseBBMapPath) || !IsPathExists(oldKernelPath)) {
        WARN_LOG("can not find bb map file or old kernel file.");
        return;
    }
    if (!Chmod(baseBBMapPath, SAVE_DATA_FILE_AUTHORITY)) {
        return;
    }
    uint64_t lastLaunchId {0ULL};
    {
        std::lock_guard<std::mutex> lk(lastLaunchIdMapMtx_);
        if (lastLaunchId_.find(regId) == lastLaunchId_.end()) {
            lastLaunchId_[regId] = 0;
        } else {
            lastLaunchId_[regId]++;
        }
        lastLaunchId = lastLaunchId_[regId];
    }
    std::string curBBMapPath = JoinPath({outDir,
        GetOutFileName(DataType::BLOCKMAP, regId) + "." + to_string(lastLaunchId)});
    std::string curKernelPath = JoinPath({outDir, GetOutFileName(DataType::KERNEL, regId)});
    if (!CopyFile(baseBBMapPath, curBBMapPath) || !CopyFile(oldKernelPath, curKernelPath)) {
        WARN_LOG("copy bb map or old kernel file failed.");
    }
}

bool BBCountDumper::GetLaunchCount(const uint64_t regId, uint64_t &count) const
{
    if (lastLaunchId_.find(regId) == lastLaunchId_.end()) {
        return false;
    }
    count = lastLaunchId_.at(regId);
    return true;
}
