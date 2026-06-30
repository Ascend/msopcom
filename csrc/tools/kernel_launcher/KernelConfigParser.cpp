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


#include "KernelConfigParser.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <algorithm>
#include "utils/FileSystem.h"
#include "utils/InjectLogger.h"
#include "utils/Ustring.h"
#include "utils/Path.h"

namespace {
constexpr char const *BIN_PATH = "bin_path";
constexpr char const *BLOCK_DIM = "block_dim";
constexpr char const *DEVICE_ID = "device_id";
constexpr char const *FFTS = "ffts";
constexpr char const *INPUT_PATH = "input_path";
constexpr char const *INPUT_SIZE = "input_size";
constexpr char const *KERNEL_NAME = "kernel_name";
constexpr char const *MAGIC = "magic";
constexpr char const *TILING_DATA = "tiling_data_path";
constexpr char const *TILING_KEY = "tiling_key";
constexpr char const *OUTPUT_SIZE = "output_size";
constexpr char const *WORKSPACE_SIZE = "workspace_size";
constexpr char const *OUTPUT_DIR = "output_dir";
constexpr char const *OUTPUT_NAME = "output_name";
constexpr char const *OLD_MODE = "old_mode";

const std::vector<std::string> MAGICS = {"RT_DEV_BINARY_MAGIC_ELF_AIVEC",
                                         "RT_DEV_BINARY_MAGIC_ELF_AICUBE",
                                         "RT_DEV_BINARY_MAGIC_ELF"};
}

using Param = KernelConfig::Param;

namespace {
bool Cmp(const std::pair<std::string, std::string>& lhs, const std::pair<std::string, std::string>& rhs)
{
    static const std::map<std::string, uint8_t> ARGS_ORDERS{
        {BIN_PATH, 0},
        {BLOCK_DIM, 1},
        {DEVICE_ID, 2},
        {FFTS, 3},
        {INPUT_PATH, 4},
        {INPUT_SIZE, 5},
        {KERNEL_NAME, 6},
        {MAGIC, 7},
        {OUTPUT_DIR, 8},
        {OUTPUT_NAME, 9},
        {OUTPUT_SIZE, 10},
        {WORKSPACE_SIZE, 11},
        {TILING_DATA, 12},
        {TILING_KEY, 13},
        {OLD_MODE, 14},
    };
    auto it0 = ARGS_ORDERS.find(lhs.first);
    auto it1 = ARGS_ORDERS.find(rhs.first);
    if (it0 == ARGS_ORDERS.end() || it1 == ARGS_ORDERS.end()) {
        return true;
    }
    // stable sort: cmp(a, a) must be false
    return it0->second < it1->second;
}
}

KernelConfigParser::KernelConfigParser()
{
    parserFuncs_[KERNEL_NAME] = std::bind(&KernelConfigParser::SetKernelName, this, std::placeholders::_1);
    parserFuncs_[BIN_PATH] = std::bind(&KernelConfigParser::SetBinPath, this, std::placeholders::_1);
    parserFuncs_[BLOCK_DIM] = std::bind(&KernelConfigParser::SetBlockDim, this, std::placeholders::_1);
    parserFuncs_[DEVICE_ID] = std::bind(&KernelConfigParser::SetDeviceId, this, std::placeholders::_1);
    parserFuncs_[MAGIC] = std::bind(&KernelConfigParser::SetMagic, this, std::placeholders::_1);
    parserFuncs_[FFTS] = std::bind(&KernelConfigParser::SetFfts, this, std::placeholders::_1);
    parserFuncs_[INPUT_PATH] = std::bind(&KernelConfigParser::SetInputPath, this, std::placeholders::_1);
    parserFuncs_[INPUT_SIZE] = std::bind(&KernelConfigParser::SetInputSize, this, std::placeholders::_1);
    parserFuncs_[TILING_DATA] = std::bind(&KernelConfigParser::SetTilingDataPath, this, std::placeholders::_1);
    parserFuncs_[TILING_KEY] = std::bind(&KernelConfigParser::SetTilingKey, this, std::placeholders::_1);
    parserFuncs_[OUTPUT_SIZE] = std::bind(&KernelConfigParser::SetOutputSize, this, std::placeholders::_1);
    parserFuncs_[WORKSPACE_SIZE] = std::bind(&KernelConfigParser::SetWorkspaceSize, this, std::placeholders::_1);
    parserFuncs_[OUTPUT_DIR] = std::bind(&KernelConfigParser::SetOutputDir, this, std::placeholders::_1);
    parserFuncs_[OUTPUT_NAME] = std::bind(&KernelConfigParser::SetOutputName, this, std::placeholders::_1);
    parserFuncs_[OLD_MODE] = std::bind(&KernelConfigParser::SetOldMode, this, std::placeholders::_1);
}

bool KernelConfigParser::SetKernelName(const std::string &arg)
{
    if (!CheckInputStringValid(arg, FILE_NAME_LENGTH_LIMIT)) {
        ERROR_LOG("kernel_name: %s is invalid.", arg.c_str());
        return false;
    }
    kernelConfig_.kernelName = arg;
    return true;
}

bool KernelConfigParser::SetBinPath(const std::string &arg)
{
    kernelConfig_.kernelBinaryPath = arg;
    return true;
}

bool KernelConfigParser::SetBlockDim(const std::string &arg)
{
    bool res = StringToNum<int32_t>(arg, kernelConfig_.blockDim);
    if (!res) {
        ERROR_LOG("Block id is invalid , blockDim: %s", arg.c_str());
        return false;
    }
    return true;
}

bool KernelConfigParser::SetDeviceId(const std::string &arg)
{
    bool res = StringToNum<int32_t>(arg, kernelConfig_.deviceID);
    if (!res) {
        ERROR_LOG("SetDeviceId failed, deviceID: %s", arg.c_str());
        return false;
    }
    return true;
}

bool KernelConfigParser::SetMagic(const std::string &arg)
{
    kernelConfig_.magic = arg;
    if (std::find(MAGICS.begin(), MAGICS.end(), arg) == MAGICS.end()) {
        ERROR_LOG("Invalid magic value.");
        return false;
    }
    return true;
}

bool KernelConfigParser::SetFfts(const std::string &arg)
{
    if (!arg.empty() && arg[0] == 'Y') {
        Param param;
        param.type = "fftsAddr";
        param.dataSize = 64U; // 64 is ffts default value
        kernelConfig_.params.emplace_back(param);
        DEBUG_LOG("Set ffts on success.");
    }
    return true;
}

bool KernelConfigParser::SetInputPath(const std::string &arg)
{
    const std::string& path = arg;
    std::vector<std::string> sizeVec;
    SplitString(path, ';', sizeVec);
    if (sizeVec.empty()) {
        ERROR_LOG("Input path does not contain any elements");
        return false;
    }
    inputCount_ = sizeVec.size();
    for (const auto& binPath : sizeVec) {
        Param param;
        param.type = "input";
        param.dType = "int8";
        param.dataPath = binPath;
        if (param.dataPath == "n") {
            param.isRequired = false;
            DEBUG_LOG("Get null data");
        } else {
            param.isRequired = true;
            DEBUG_LOG("Get bin data, data path is %s", binPath.c_str());
        }
        kernelConfig_.params.emplace_back(param);
    }
    return true;
}

bool KernelConfigParser::SetInputSize(const std::string &arg)
{
    const std::string& path = arg;
    std::vector<std::string> sizeVec;
    SplitString(path, ';', sizeVec);
    if (inputCount_ == 0) {
        ERROR_LOG("Input path should be entered first");
        return false;
    }
    if (sizeVec.size() != inputCount_) {
        ERROR_LOG("The count of input path and input size should be equal, path size is %d, size count is %zu",
                  inputCount_, sizeVec.size());
        return false;
    }
    uint32_t i = 0;
    for (auto &param : kernelConfig_.params) {
        if (param.type != "input") {
            DEBUG_LOG("Params type is %s, ignore in set input size", param.type.c_str());
            continue;
        }
        bool res = StringToNum<size_t>(sizeVec[i], param.dataSize);
        if (!res) {
            ERROR_LOG("Set input bin size failed, size: %s", sizeVec[i].c_str());
            return false;
        }
        i++;
    }
    return true;
}

bool KernelConfigParser::SetOutputSize(const std::string &arg)
{
    std::vector<std::string> sizeVec;
    SplitString(arg, ';', sizeVec);
    uint32_t i = 0;
    if (outputCount_ == 0 && sizeVec.size() != 0) {
        INFO_LOG("No out put name, not parse output size");
        return true;
    }
    if (outputCount_ != sizeVec.size()) {
        ERROR_LOG("The count of out name and out size should be equal, name count is %d, size is %zu",
                  outputCount_, sizeVec.size());
        return false;
    }
    for (auto &param : kernelConfig_.params) {
        if (param.type != "output") {
            continue;
        }
        bool res = StringToNum<size_t>(sizeVec[i], param.dataSize);
        if (!res) {
            ERROR_LOG("Set output bin size failed, size: %s", sizeVec[i].c_str());
            return false;
        }
        i++;
    }
    return true;
}

bool KernelConfigParser::SetWorkspaceSize(const std::string &arg)
{
    std::vector<std::string> sizeVec;
    std::vector<Param> paras;
    SplitString(arg, ';', sizeVec);
    for (uint32_t i = 0; i < sizeVec.size(); i++) {
        Param param;
        param.type = "workspace";
        bool res = StringToNum<size_t>(sizeVec[i], param.dataSize);
        if (!res) {
            ERROR_LOG("Set workspace size failed, size: %s", sizeVec[i].c_str());
            return false;
        }
        paras.push_back(param);
    }
    kernelConfig_.params.insert(kernelConfig_.params.end(), paras.begin(), paras.end());
    return true;
}

bool KernelConfigParser::SetOutputDir(const std::string &arg)
{
    // 1.规范化绝对路径
    std::string canonicalPath = Path(arg).PathCanonicalize().ToString();
    if (canonicalPath.empty()) {
        ERROR_LOG("output_dir: cannot resolve path: %s", arg.c_str());
        return false;
    }
    // 2.获取当前工作目录作为基目录
    std::string currentPath;
    if (!GetCurrentPath(currentPath)) {
        ERROR_LOG("output_dir: cannot get current working dir.");
        return false;
    }
    // 3.验证路径在当前工作目录下（或指定的安全基目录下）
    if (canonicalPath.rfind(currentPath, 0) != 0 ||
        (canonicalPath.length() > currentPath.length() && canonicalPath[currentPath.length()] != '/')) {
        ERROR_LOG("output_dir: path %s is outside allowed directory %s", canonicalPath.c_str(), currentPath.c_str());
        return false;
    }
    // 4.检查路径长度
    if (!PathLenCheckValid(canonicalPath)) {
        ERROR_LOG("output_dir: path too long: %s", canonicalPath.c_str());
        return false;
    }
    kernelConfig_.outputDir = canonicalPath;
    return true;
}

bool KernelConfigParser::SetOldMode(const std::string &arg)
{
    kernelConfig_.isOldVersion = (arg == "1");
    return true;
}

bool KernelConfigParser::SetOutputName(const std::string &arg)
{
    std::vector<std::string> nameVec;
    SplitString(arg, ';', nameVec);
    for (uint32_t i = 0; i < nameVec.size(); i++) {
        // 添加路径分隔符和特殊字符检查
        if (!CheckInputStringValid(nameVec[i], FILE_NAME_LENGTH_LIMIT)) {
            ERROR_LOG("output_name: %s is invalid. (contains illegal characters)", nameVec[i].c_str());
            return false;
        }
        Param param;
        param.type = "output";
        param.name = nameVec[i];
        kernelConfig_.params.push_back(param);
    }
    outputCount_ = nameVec.size();
    return true;
}

bool KernelConfigParser::SetTilingDataPath(const std::string &arg)
{
    if (!arg.empty()) {
        std::vector<std::string> sizeVec;
        SplitString(arg, ';', sizeVec);
        Param param;
        param.type = "tiling";
        param.dataPath = sizeVec[0];
        if (sizeVec.size() < 2U) {
            WARN_LOG("Tiling data format error");
            return false;
        }

        bool res = StringToNum<size_t>(sizeVec[1], param.dataSize);
        if (!res) {
            WARN_LOG("Set tiling data failed, blockDim: %s", sizeVec[1].c_str());
            return false;
        }
        kernelConfig_.params.emplace_back(param);
    }
    return true;
}

bool KernelConfigParser::SetTilingKey(const std::string &arg)
{
    if (!arg.empty()) {
        bool res = StringToNum<uint64_t>(arg, kernelConfig_.tilingKey);
        if (!res) {
            WARN_LOG("Set tiling key failed");
            return false;
        }
        DEBUG_LOG("Set tiling key success, tiling_key is %lu", kernelConfig_.tilingKey);
        kernelConfig_.hasTilingKey = true;
    }
    return true;
}

KernelConfig KernelConfigParser::GetKernelConfig(const std::string &filePath)
{
    nlohmann::json argsJson;
    if (!GetJsonFromBin(filePath, argsJson)) {
        return kernelConfig_;
    }
    std::vector<std::pair<std::string, std::string>> items;
    for (auto& item : argsJson.items()) {
        items.push_back({item.key(), item.value()});
    }
    std::sort(items.begin(), items.end(), Cmp);
    for (const auto &item : items) {
        auto iter = parserFuncs_.find(item.first);
        if (iter == parserFuncs_.end()) {
            WARN_LOG("Cannot find args processing function, args key is %s", item.first.c_str());
            continue;
        }
        if (!iter->second(item.second)) {
            ERROR_LOG("Parser args failed, args key is %s", item.first.c_str());
            return kernelConfig_;
        }
        DEBUG_LOG("Parser args success, args key is %s, value is %s", item.first.c_str(), item.second.c_str());
    }
    isGetConfigSuccess_ = true;
    return kernelConfig_;
}

bool KernelConfigParser::GetJsonFromBin(const std::string &filePath, nlohmann::json &jsonData) const
{
    size_t fileSize = GetFileSize(filePath);
    std::vector<char> rawFile(fileSize);
    if (ReadBinary(filePath, rawFile) == 0) {
        ERROR_LOG("Bin read failed, path is %s", filePath.c_str());
        return false;
    }
    if (nlohmann::json::accept(rawFile.begin(), rawFile.end())) {
        jsonData = nlohmann::json::parse(rawFile.begin(), rawFile.end(), nullptr, false);
    } else {
        ERROR_LOG("Bin file to json parser failed, path is %s", filePath.c_str());
        return false;
    }
    return true;
}
