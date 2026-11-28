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

#ifndef KERNEL_LAUNCHER_KERNEL_CONFIG_PARSER_H
#define KERNEL_LAUNCHER_KERNEL_CONFIG_PARSER_H

#include <functional>
#include <string>

#include "nlohmann/json.hpp"

constexpr size_t MAX_KERNEL_INPUT_FILE_SIZE = 90ULL * 1024 * 1024 * 1024;

struct KernelConfig {
    struct Param {
        std::string type;
        std::string name;
        std::string dType;
        size_t dataSize;
        std::string dataPath;
        bool isRequired;
    };
    bool hasTilingKey {false};
    bool isOldVersion {true};
    uint64_t tilingKey {0};
    int blockDim {0};
    int deviceID {0};
    std::string kernelName;
    std::string kernelBinaryPath;
    std::string magic;
    std::string outputDir;
    std::vector<Param> params;
};

class KernelConfigParser {
public:
    KernelConfig GetKernelConfig(const std::string &filePath);
    KernelConfigParser();

    bool isGetConfigSuccess_ {false};
private:
    bool SetKernelName(const std::string &arg);
    bool SetBinPath(const std::string &arg);
    bool SetBlockDim(const std::string &arg);
    bool SetDeviceId(const std::string &arg);
    bool SetMagic(const std::string &arg);
    bool SetFfts(const std::string &arg);
    bool SetInputPath(const std::string &arg);
    bool SetInputSize(const std::string &arg);
    bool SetTilingDataPath(const std::string &arg);
    bool SetOutputSize(const std::string &arg);
    bool SetOutputDir(const std::string &arg);
    bool SetWorkspaceSize(const std::string &arg);
    bool SetTilingKey(const std::string &arg);
    bool GetJsonFromBin(const std::string &filePath, nlohmann::json &jsonData) const;
    bool SetOutputName(const std::string &arg);
    bool SetOldMode(const std::string &arg);

    std::map<std::string, std::function<bool (const std::string &arg)>> parserFuncs_;
    KernelConfig kernelConfig_;
    uint32_t inputCount_ {0};
    uint32_t outputCount_ {0};
};
#endif // KERNEL_LAUNCHER_KERNEL_CONFIG_PARSER_H
