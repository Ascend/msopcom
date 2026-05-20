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


#include "acl_rt_impl/AscendclImplOrigin.h"
#include "utils/FileSystem.h"
#include "utils/InjectLogger.h"

#include "ArgsContext.h"

using namespace std;

bool DumperContext::ToJson(nlohmann::json &jsonData) const
{
    if (isAclNew) {
        jsonData["old_mode"] = "0";
    }
    jsonData["bin_path"] = binPath;
    jsonData["block_dim"] = to_string(blockDim);
    jsonData["device_id"] = std::to_string(devId);
    jsonData["ffts"] = isFFTS ? "Y" : "N";
    jsonData["input_path"] = Join(inputPathList.begin(), inputPathList.end(), ";");
    vector<string> tmpSlices;
    for (const auto &v: inputSizeList) {
        tmpSlices.push_back(to_string(v));
    }
    jsonData["input_size"] = Join(tmpSlices.begin(), tmpSlices.end(), ";");
    jsonData["kernel_name"] = kernelName;

    if (magic == rtDevBinaryMagicElfAivec) {
        jsonData["magic"] = "RT_DEV_BINARY_MAGIC_ELF_AIVEC";
    } else if (magic == rtDevBinaryMagicElfAicube) {
        jsonData["magic"] = "RT_DEV_BINARY_MAGIC_ELF_AICUBE";
    } else if (magic == rtDevBinaryMagicElf) {
        jsonData["magic"] = "RT_DEV_BINARY_MAGIC_ELF";
    } else {
        return false;
    }
    tmpSlices.clear();
    if (!tilingDataPath.empty()) {
        tmpSlices.push_back(tilingDataPath);
        tmpSlices.push_back(to_string(tilingDataSize));
        jsonData["tiling_data_path"] = Join(tmpSlices.begin(), tmpSlices.end(), ";");
    }
    if (hasTilingKey) {
        jsonData["tiling_key"] = to_string(tilingKey);
    }
    return true;
}

bool DumpInputData(const string &outputDir,
                   const vector<AddrInfo> &inputParamsAddrInfos,
                   DumperContext &config)
{
    if (inputParamsAddrInfos.empty()) {
        INFO_LOG("Addr info is null, can not dump");
        return false;
    }
    for (size_t i = 0; i < inputParamsAddrInfos.size(); i++) {
        const auto &addrInfo = inputParamsAddrInfos[i];
        config.inputSizeList.emplace_back(addrInfo.length);
        if (addrInfo.length == 0) {
            // According to the kernel-launcher process, if the current input is empty, set 'n'.
            config.inputPathList.emplace_back("n");
            continue;
        }
        constexpr uint64_t MAX_MEM_SIZE = 10ULL * 1024 * 1024 * 1024;
        if (addrInfo.length > MAX_MEM_SIZE) {
            ERROR_LOG("Kernel input size too large, %lu > 10G", addrInfo.length);
            return false;
        }
        void *hostData = nullptr;
        if (aclrtMallocHostImplOrigin(&hostData, addrInfo.length) != ACL_ERROR_NONE) {
            return false;
        }
        using Defer = std::shared_ptr<void>;
        Defer defer0(nullptr, [&hostData](std::nullptr_t&) {
            if (aclrtFreeHostImplOrigin(hostData) != ACL_ERROR_NONE) {
                ERROR_LOG("rtFreeHost failed\n");
            }
        });
        if (addrInfo.addr == 0U) {
            DEBUG_LOG("Invalid args addr=0.");
            return false;
        }
        if (aclrtMemcpyImplOrigin(hostData, addrInfo.length, reinterpret_cast<void*>(addrInfo.addr),
            addrInfo.length, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_ERROR_NONE) {
            return false;
        }

        std::string inputPath = outputDir + "/input_" + to_string(i) + ".bin";
        config.inputPathList.emplace_back(inputPath);
        size_t written = WriteBinary(inputPath, static_cast<const char *>(hostData), addrInfo.length);
        if (written != addrInfo.length) {
            return false;
        }
    }
    return true;
}

bool DumpTilingData(const std::string &outputDir, std::vector<uint8_t> const &tilingData, DumperContext &config)
{
    if (tilingData.empty()) {
        DEBUG_LOG("no tiling data");
        return true;
    }

    config.tilingDataPath = outputDir + "/input_tiling.bin";
    config.tilingDataSize = tilingData.size();
    size_t written = WriteBinary(config.tilingDataPath, reinterpret_cast<const char*>(tilingData.data()), tilingData.size());
    if (written != tilingData.size()) {
        return false;
    }
    return true;
}
