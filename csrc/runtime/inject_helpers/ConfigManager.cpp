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

#include "ConfigManager.h"
#include <mutex>
#include <string>

#include "utils/Future.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"
#include "core/BinaryInstrumentation.h"
#include "runtime/inject_helpers/KernelMatcher.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/ThreadContext.h"
#include "runtime/inject_helpers/DBITask.h"

SanitizerConfig const &ConfigManager<SanitizerConfig>::GetConfig() const
{
    // 当前工具侧发给客户端的配置是完全相同的，每次C/S建立连接后，Server都会发送一次Config
    // 为避免来自Server的Config消息被错位接收，需要此方法在接收Server消息(如kernelBlock Response)前调用
    static std::mutex mutex;
    static std::unordered_map<int32_t, std::unique_ptr<SanitizerConfig>> configMap;
    int32_t deviceId = ThreadContext::Instance().GetDeviceId();
    std::lock_guard<std::mutex> lk(mutex);
    auto it = configMap.find(deviceId);
    if (it != configMap.end()) {
        DBITaskConfig::Instance().Init(BIType::CUSTOMIZE, it->second->pluginPath, KernelMatcher::Config{});
        return *it->second;
    }
    // 查不到device的config说明还没有接收
    auto configPtr = MakeUnique<SanitizerConfig>();
    std::string msg;
    constexpr uint32_t timeOut = 5;
    auto readSize = LocalDevice::GetInstance(deviceId).Wait(msg, timeOut);
    if (readSize == sizeof(SanitizerConfig)) {
        Deserialize(msg, *configPtr);
        KernelMatcher::Config matchConfig;
        configPtr->pluginPath[PLUGIN_PATH_MAX - 1] = '\0'; // 确保以空字符结尾
        configPtr->kernelName[KERNEL_NAME_MAX - 1] = '\0';
        configPtr->dumpPath[DUMP_PATH_MAX - 1] = '\0';
        matchConfig.kernelName = configPtr->kernelName;
        DBITaskConfig::Instance().Init(BIType::CUSTOMIZE, configPtr->pluginPath, matchConfig);
        KernelDumper::Instance().Init(configPtr->dumpPath, matchConfig);
    } else {
        ERROR_LOG("Init sanitizer config failed. Invalid message received. expected size=%zu, received size=%zu\n",
                  sizeof(SanitizerConfig), readSize);
    }
    configMap.emplace(deviceId, std::move(configPtr));
    return *configMap[deviceId];
}
