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

#include "KernelReplacement.h"

#include <fstream>
#include "core/PlatformConfig.h"
#include "runtime/RuntimeOrigin.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/LaunchContext.h"
#include "runtime/inject_helpers/RegisterManager.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "utils/InjectLogger.h"

using namespace std;
namespace {
constexpr uint32_t MAX_DUMP_NUM = 50000;
}

bool KernelReplacement::CreateHandle(void **handle, uint64_t launchId)
{
    if (!handle || kernelPath_.empty() || !matcher_) {
        return false;
    }

    string kernelName = KernelContext::Instance().GetLaunchName();
    if (!matcher_->Match(launchId, kernelName)) {
        return false;
    }

    DEBUG_LOG("launchId=%lu, kernelName=%s, kernelPath_=%s\n", launchId, kernelName.c_str(), kernelPath_.c_str());
    uint64_t regId = KernelContext::Instance().GetRegisterId(launchId);
    if (regId == UINT64_MAX) {
        return false;
    }
    if (oldKernelRegId_ != UINT64_MAX) {
        if (oldKernelRegId_ != regId || !replaceTask_) {
            return false;
        }
        *handle = replaceTask_->GetHandle();
        return true;
    }

    replaceTask_.reset(new KernelReplaceTask(kernelPath_)); // NOLINT(cppcoreguidelines-owning-memory)
    if (replaceTask_->Run(handle, regId, false)) {
        oldKernelRegId_ = regId;
        return true;
    }
    replaceTask_.reset();
    return true;
}

bool KernelReplaceTask::Run(void **handle, uint64_t regId, bool withStubFunc)
{
    KernelContext::RegisterEvent event;
    bool ret = KernelContext::Instance().GetRegisterEvent(regId, event);
    if (!ret) {
        return false;
    }

    if (ReadBinary(kernelPath_, binaryData_) == 0) {
        ERROR_LOG("CreateHandle: read binary failed.");
        return false;
    }
    bin_ = event.bin;
    // only fix binary data
    bin_.data = binaryData_.data();
    bin_.length = binaryData_.size();
    auto registerFunc = withStubFunc ? rtDevBinaryRegisterOrigin : rtRegisterAllKernelOrigin;
    auto rtRet = registerFunc(&bin_, &handle_);
    if (rtRet != RT_ERROR_NONE) {
        ERROR_LOG("CreateHandle: register the new replaced kernel failed.");
        return false;
    }
    *handle = handle_;
    return true;
}

void *KernelReplaceTask::GetHandle()
{
    return handle_;
}

KernelReplaceTask::~KernelReplaceTask()
{
    if (handle_) {
        rtDevBinaryUnRegisterOrigin(handle_);
        handle_ = nullptr;
    }
    if (binHandle_) {
        aclrtBinaryUnLoadImplOrigin(binHandle_);
        binHandle_ = nullptr;
    }
}

bool KernelReplacement::ReleaseHandle(const void *handle)
{
    if (!replaceTask_) {
        return false;
    }

    uint64_t regId = KernelContext::Instance().GetRegisterId(handle, true);
    if (regId == UINT64_MAX || regId != oldKernelRegId_) {
        return false;
    }
    replaceTask_.reset();
    oldKernelRegId_ = UINT64_MAX;
    return true;
}

struct CallbackArgs {
    uint64_t launchId;
    bool aclNew;
};

void KernelDumper::DumpDataCallback(void *fnData)
{
    CallbackArgs args = *static_cast<CallbackArgs *>(fnData);
    KernelDumper::Instance().Dump(args.launchId, args.aclNew);
}

bool KernelDumper::LaunchDumpTask(rtStream_t stm, bool aclNew)
{
    static std::vector<std::shared_ptr<CallbackArgs>> callbackArgs;
    dumpNum_++;
    if (dumpNum_ > MAX_DUMP_NUM) {
        DEBUG_LOG("Detect too much kernel, exceed limit %u, ignored remain", MAX_DUMP_NUM);
        return false;
    }
    auto &deviceContext = KernelContext::Instance().GetDeviceContext();
    if (!deviceContext.SubscribeReport(stm)) {
        return false;
    }
    uint64_t launchId;
    if (aclNew) {
        LaunchContextSP lastLaunchCtx = LaunchManager::Local().GetLastContext();
        if (lastLaunchCtx == nullptr) {
            ERROR_LOG("Empty launch context list yet.");
            return false;
        }
        launchId = lastLaunchCtx->GetLaunchId();
    } else {
        launchId = deviceContext.GetLaunchId();
    }
    auto args = MakeShared<CallbackArgs>();
    args->launchId = launchId;
    args->aclNew = aclNew;
    callbackArgs.emplace_back(args);
    if (aclNew) {
        auto ret = aclrtLaunchCallbackImplOrigin(
            &KernelDumper::DumpDataCallback, static_cast<void *>(args.get()), ACL_CALLBACK_BLOCK, stm);
        if (ret != ACL_ERROR_NONE) {
            ERROR_LOG("launch callback dump task failed, ret=%u", ret);
            return false;
        }
    } else {
        auto ret = rtCallbackLaunchOrigin(&KernelDumper::DumpDataCallback, static_cast<void *>(args.get()), stm, true);
        if (ret != RT_ERROR_NONE) {
            ERROR_LOG("launch callback dump task failed, ret=%u", ret);
            return false;
        }
    }
    return true;
}

bool KernelDumper::Dump(uint64_t launchId, bool aclNew) const
{
    string kernelName;
    if (aclNew) {
        auto launchCtx = LaunchManager::Local().GetContext(launchId);
        if (launchCtx == nullptr) {
            WARN_LOG("Get context with launchId failed. launchId:%lu", launchId);
            return false;
        }
        kernelName = launchCtx->GetFuncContext()->GetKernelName();
    } else {
        kernelName = KernelContext::Instance().GetLaunchName(launchId);
    }
    if (!matcher_ || !matcher_->Match(launchId, kernelName)) {
        return false;
    }
    int32_t devId = DeviceContext::Local().GetDeviceId();
    string dirName = "dumpData_" + to_string(devId) + "_" + to_string(launchId);
    string subOutputDir = JoinPath({outputDir_, dirName});
    return Dump(subOutputDir, launchId, aclNew);
}

bool KernelDumper::Dump(const string &outputDir, uint64_t launchId, bool aclNew) const
{
    MkdirRecusively(outputDir);
    if (aclNew) {
        auto launchCtx = LaunchManager::Local().GetContext(launchId);
        if (launchCtx == nullptr) {
            ERROR_LOG("get launch context failed, invalid launch id:%lu", launchId);
            return false;
        }
        return launchCtx->Save(outputDir);
    } else {
        return KernelContext::Instance().Save(outputDir, launchId);
    }
}

bool KernelDumper::DumpAicore(const string &outputDir) const
{
    return KernelContext::Instance().SaveAicore(outputDir);
}
