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

#include "RegisterManager.h"
#include "utils/InjectLogger.h"
#include "utils/TypeTraits.h"

#include "nlohmann/json.hpp"

using namespace std;

bool ParseMagicStr(std::string const &magicStr, uint32_t &magic)
{
    static const std::map<std::string, uint32_t> MAGIC_STR_MAP = {
        { "RT_DEV_BINARY_MAGIC_ELF_AIVEC", RT_DEV_BINARY_MAGIC_ELF_AIVEC },
        { "RT_DEV_BINARY_MAGIC_ELF_AICUBE", RT_DEV_BINARY_MAGIC_ELF_AICUBE },
        { "RT_DEV_BINARY_MAGIC_ELF", RT_DEV_BINARY_MAGIC_ELF },
        { "RT_DEV_BINARY_MAGIC_ELF_AICPU", RT_DEV_BINARY_MAGIC_ELF_AICPU },
    };
    auto it = as_const(MAGIC_STR_MAP).find(magicStr);
    if (it == MAGIC_STR_MAP.cend()) {
        return false;
    }
    magic = it->second;
    return true;
}

bool ReadMagicFromKernelJson(std::string const &jsonPath, std::string &magicStr)
{
    std::string resolved = Realpath(jsonPath);
    if (resolved.empty()) {
        WARN_LOG("Kernel JSON path not exist. path:%s", ToSafeString(jsonPath).c_str());
        return false;
    }
    if (!CheckInputFileValid(resolved, "json")) {
        WARN_LOG("Kernel JSON path is invalid. path:%s", ToSafeString(resolved).c_str());
        return false;
    }

    std::ifstream ifs(resolved);
    nlohmann::json kernelConfig;
    try {
        kernelConfig = nlohmann::json::parse(ifs);
    } catch (nlohmann::json::parse_error& ex) {
        WARN_LOG("Parse kernel JSON failed. JSON data:%s", ex.what());
        return false;
    }

    if (!kernelConfig.contains("magic") || !kernelConfig["magic"].is_string()) {
        WARN_LOG("Get magic field from kernel JSON failed.");
        return false;
    }
    magicStr = kernelConfig["magic"].get<std::string>();
    return true;
}

RegisterContextSP RegisterManager::CreateContext(const char *binPath, aclrtBinHandle binHandle, uint32_t magic,
                                                 aclrtBinaryLoadOptions *options)
{
    // load magic from kernel json file
    auto ctx = make_shared<RegisterContext>();
    bool optionsIsNull{};
    if (options == nullptr) { optionsIsNull = true; }
    RegisterParam param{currentRegId_, magic, options, false, optionsIsNull};
    if (!ctx->Init(binPath, binHandle, param)) {
        return nullptr;
    }
    currentRegId_++;
    std::lock_guard<std::mutex> lock(mtx_);
    contexts_[binHandle] = ctx;
    return ctx;
}

RegisterContextSP RegisterManager::CreateContext(aclrtBinary bin, aclrtBinHandle binHandle, uint32_t magic,
                                                 aclrtBinaryLoadOptions *options)
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto ctx = make_shared<RegisterContext>();
    const auto &data = tempEflData_[bin];
    if (data.empty()) {
        DEBUG_LOG("Can not find elf data");
        return nullptr;
    }
    if (options) {
        DEBUG_LOG("Got options num: %lu", options->numOpt);
    }
    bool optionsIsNull{};
    if (options == nullptr) { optionsIsNull = true; }
    RegisterParam param{currentRegId_, magic, options, false, optionsIsNull};
    if (!ctx->Init(data, binHandle, param)) {
        return nullptr;
    }
    currentRegId_++;
    contexts_[binHandle] = ctx;
    return ctx;
}

RegisterContextSP RegisterManager::CreateContext(const void *data, size_t length,
    aclrtBinHandle binHandle, uint32_t magic, aclrtBinaryLoadOptions *options)
{
    auto ctx = make_shared<RegisterContext>();
    // check length
    const char *elfDataPtr = static_cast<const char*>(data);
    vector<char> elfData(elfDataPtr, elfDataPtr + length);
    bool optionsIsNull{};
    if (options == nullptr) { optionsIsNull = true; }
    RegisterParam param{currentRegId_, magic, options, false, optionsIsNull};
    if (!ctx->Init(std::move(elfData), binHandle, param)) {
        return nullptr;
    }
    currentRegId_++;
    std::lock_guard<std::mutex> lock(mtx_);
    contexts_[binHandle] = ctx;
    return ctx;
}

RegisterContextSP RegisterManager::GetContext(void *key)
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = contexts_.find(key);
    if (it != contexts_.end()) {
        return it->second;
    }
    return nullptr;
}

void RegisterManager::CacheElfData(aclrtBinary bin, const char *data, size_t dataLen)
{
    // check dataLen
    if (dataLen > MAX_MEM_BYTE_SIZE) {
        WARN_LOG("Elf data size (%zu bytes) exceeds maximum allowed size (%zu)", dataLen, MAX_MEM_BYTE_SIZE);
        return;
    }
    std::lock_guard<std::mutex> lock(mtx_);
    tempEflData_[bin] = vector<char>(data, data + dataLen);
}
