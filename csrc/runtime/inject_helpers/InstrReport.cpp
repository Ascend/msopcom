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


#include "InstrReport.h"

#include <iostream>

#include "ConfigManager.h"
#include "DevMemManager.h"
#include "KernelContext.h"
#include "DeviceContext.h"
#include "KernelMatcher.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/HijackedLayerManager.h"
#include "FuncContext.h"
#include "LaunchContext.h"
#include "LaunchManager.h"
#include "LocalDevice.h"
#include "RegisterContext.h"
#include "MemoryDataCollect.h"
#include "ArgsManager.h"
#include "utils/InjectLogger.h"
#include "core/PlatformConfig.h"
#include "runtime.h"
#include "utils/Protocol.h"
#include "InteractHelper.h"
#include "utils/Numeric.h"
#include "utils/Ustring.h"

namespace {

inline bool CheckBlockDimValid(uint64_t blockDim)
{
    if (blockDim == 0) {
        ERROR_LOG("blockDim must be greater than 0");
        return false;
    } else if (blockDim > MAX_BLOCKDIM_NUMS) {
        ERROR_LOG("blockDim set as %lu exceeds then maximum value %lu", blockDim, MAX_BLOCKDIM_NUMS);
        return false;
    }
    return true;
}

} // namespace [Dummy]


std::string GetArchName(KernelType kernelType, const std::string &socVersion)
{
    if (socVersion.find("Ascend910B") != std::string::npos ||
        socVersion.find("Ascend910_93") != std::string::npos) {
        switch (kernelType) {
            case KernelType::AIVEC:
                return "dav-c220-vec";
            case KernelType::AICUBE:
                return "dav-c220-cube";
            case KernelType::MIX:
                return "dav-c220";
            default:
                return "";
        }
    }

    if (socVersion.find("Ascend310P") != std::string::npos) {
        switch (kernelType) {
            case KernelType::AIVEC:
                return "dav-m200-vec";
            case KernelType::MIX:
                return "dav-m200";
            default:
                return "";
        }
    }

    if (socVersion.find("Ascend950") != std::string::npos) {
        switch (kernelType) {
            case KernelType::AIVEC:
                return "dav-c310-vec";
            case KernelType::AICUBE:
                return "dav-c310-cube";
            case KernelType::MIX:
                return "dav-c310";
            default:
                return "";
        }
    }
    return "";
}

std::string GetCurrentArchName()
{
    KernelType kernelType = GetCurrentKernelType();
    std::string socVersion = DeviceContext::Local().GetSocVersion();
    return GetArchName(kernelType, socVersion);
}

bool SkipSanitizer(std::string const &kernelName)
{
    // 使用工具侧传过来的 KernelName 判断是否需要跳过当前的 kernel 检测
    SanitizerConfig const &sanitizerConfig = SanitizerConfigManager::Instance().GetConfig();
    KernelMatcher::Config matchConfig {};
    matchConfig.kernelName = sanitizerConfig.kernelName;
    bool skipSanitizer = !KernelMatcher(matchConfig).Match(0, kernelName);
    if (skipSanitizer) {
        DEBUG_LOG("skip sanitizer for kernel name: %s", kernelName.c_str());
    } else {
        DEBUG_LOG("do sanitizer for kernel name: %s", kernelName.c_str());
    }
    return skipSanitizer;
}

std::string GetTargetArchName(const FuncContextSP &funcCtx)
{
    auto kernelType = funcCtx->GetKernelType();
    const auto &socVersion = DeviceContext::Local().GetSocVersion();
    return GetArchName(kernelType, socVersion);
}

void ReportKernelSummary(uint64_t launchId)
{
    KernelContext::LaunchEvent event;
    if (!KernelContext::Instance().GetLaunchEvent(launchId, event)) {
        return;
    }
    uint64_t registerId = KernelContext::Instance().GetRegisterId(launchId);
    KernelContext::RegisterEvent registerEvent;
    if (!KernelContext::Instance().GetRegisterEvent(registerId, registerEvent)) {
        return;
    }
    KernelSummary kernelSummary;
    kernelSummary.pcStartAddr = event.pcStartAddr;
    kernelSummary.blockDim = event.blockDim;
    kernelSummary.kernelType = GetCurrentKernelType();
    kernelSummary.isKernelWithDBI = registerEvent.isKernelWithDBI;
    kernelSummary.hasDebugLine = registerEvent.hasDebugLine;
    std::size_t length = event.kernelName.copy(kernelSummary.kernelName, sizeof(kernelSummary.kernelName) - 1);
    kernelSummary.kernelName[length] = '\0';

    PacketHead head = { PacketType::KERNEL_SUMMARY };
    LocalDevice::Local().Notify(Serialize(head, kernelSummary));
}

void ReportKernelSummary(LaunchContextSP launchCtx)
{
    if (launchCtx == nullptr) {
        return;
    }

    FuncContextSP funcCtx = launchCtx->GetDBIFuncCtx();
    if (funcCtx == nullptr) {
        funcCtx = launchCtx->GetFuncContext();
    }
    RegisterContextSP regCtx = funcCtx->GetRegisterContext();

    KernelSummary kernelSummary;
    kernelSummary.kernelType = funcCtx->GetKernelType();
    kernelSummary.pcStartAddr = funcCtx->GetStartPC();
    kernelSummary.blockDim = launchCtx->GetLaunchParam().blockDim;
    kernelSummary.isKernelWithDBI = !regCtx->HasStaticStub();
    kernelSummary.hasDebugLine = regCtx->HasDebugLine();
    std::size_t length = funcCtx->GetKernelName().copy(kernelSummary.kernelName, sizeof(kernelSummary.kernelName) - 1);
    kernelSummary.kernelName[length] = '\0';

    PacketHead head = { PacketType::KERNEL_SUMMARY };
    LocalDevice::Local().Notify(Serialize(head, kernelSummary));
}

uint8_t *__sanitizer_init(uint64_t blockDim)
{
    if (!CheckBlockDimValid(blockDim)) {
        return nullptr;
    }
    SanitizerLaunchInit launchInit;
    launchInit.Init(blockDim);
    return launchInit.Process();
}

void __sanitizer_finalize(uint8_t *memInfo, uint64_t blockDim)
{
    if (!DevMemManager::Instance().IsMemoryInit() || !CheckBlockDimValid(blockDim) || memInfo == nullptr) { return; }
    SanitizerLaunchFinalize launchFinalize;
    launchFinalize.Init(memInfo, blockDim);
    launchFinalize.Process();
}
