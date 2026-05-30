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

#include "DBITask.h"

#include <chrono>
#include <elf.h>

#include "utils/FileSystem.h"
#include "utils/ElfLoader.h"
#include "utils/Ustring.h"
#include "utils/PipeCall.h"
#include "utils/Future.h"
#include "runtime/inject_helpers/InstrReport.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "runtime/inject_helpers/DevMemManager.h"
#include "runtime/RuntimeOrigin.h"
#include "core/BinaryInstrumentation.h"
#include "core/LocalProcess.h"

using namespace std;

DBITaskFactory &DBITaskFactory::Instance()
{
    thread_local static DBITaskFactory inst;
    return inst;
}

DBITaskSP DBITaskFactory::GetOrCreate(uint64_t regId, const std::string &tilingKey,
                                      BIType type, const std::string &pluginPath)
{
    auto &task = taskPool_[regId][{type, nullptr, tilingKey, pluginPath}];
    if (!task) {
        task = MakeShared<DBITask>(type);
    }
    return task;
}

DBITaskSP DBITaskFactory::GetOrCreate(uint64_t regId, const void *stubFunc,
                                      BIType type, const std::string &pluginPath)
{
    auto &task = taskPool_[regId][{type, stubFunc, "", pluginPath}];
    if (!task) {
        task = MakeShared<DBITask>(type);
    }
    return task;
}

void DBITaskFactory::Reset()
{
    taskPool_.clear();
}

void DBITaskFactory::Destroy(uint64_t regId)
{
    auto dit = taskPool_.find(regId);
    if (dit != taskPool_.cend()) {
        taskPool_.erase(dit);
    }
}

bool DBITask::NeedConvert() const
{
    // 前提保证：aclrt接口没有用到replaceTask_, rt接口没有用到funcCtx_
    if (!replaceTask_ && !funcCtx_) {
        return true;
    }
    // 对于BBCount的插桩，外部要设置keep outputs，
    // 这里就判断下，如果设置了keep但是还没用文件夹，说明需要做插桩转换
    if (!IsDir(DBITaskConfig::Instance().GetOutputDir(launchId_)) &&
        DBITaskConfig::Instance().keepTaskOutputs_) {
        return true;
    }
    // 对于其他的插桩，都不需要判断目录，如果replaceTask存在，那么直接不需要转换，复用handle
    return false;
}

bool DBITask::ReuseConverted(void **handle, uint64_t launchId, uint64_t registerId)
{
    if (NeedConvert()) {
        return false;
    }
    *handle = replaceTask_->GetHandle();
    // update stubBin field in registerEvent
    KernelContext::Instance().SetDbiBinary(registerId, replaceTask_->GetDevBinary());
    if (DBITaskConfig::Instance().keepTaskOutputs_) {
        CreateSymlink(DBITaskConfig::Instance().GetOutputDir(launchId_),
                      DBITaskConfig::Instance().GetOutputDir(launchId));
    }
    return true;
}

bool DBITask::ReuseConverted(uint64_t launchId) const
{
    if (NeedConvert()) {
        return false;
    }
    if (DBITaskConfig::Instance().keepTaskOutputs_) {
        CreateSymlink(DBITaskConfig::Instance().GetOutputDir(launchId_),
                      DBITaskConfig::Instance().GetOutputDir(launchId));
    }
    return true;
}

FuncContextSP DBITask::Run(const LaunchContextSP &launchCtx)
{
    uint64_t launchId = launchCtx->GetLaunchId();
    if (ReuseConverted(launchId)) {
        return funcCtx_;
    }
    auto funcCtx = launchCtx->GetFuncContext();
    const auto &taskConfig = DBITaskConfig::Instance();
    string tmpLaunchDir = taskConfig.GetOutputDir(launchId);
    if (!MkdirRecusively(tmpLaunchDir)) {
        ERROR_LOG("Mkdir tmp launch dir failed, stop running dbi task.");
        return nullptr;
    }
    shared_ptr<void> defer0(nullptr, [&tmpLaunchDir](std::nullptr_t&) {
        if (!DBITaskConfig::Instance().keepTaskOutputs_) {
            RemoveAll(tmpLaunchDir);
        }
    });
    BinaryInstrumentation::Config config{taskConfig.pluginPath_, GetTargetArchName(funcCtx), tmpLaunchDir,
        taskConfig.tuneLogPath_, taskConfig.argsSize_, taskConfig.extraCompilerArgs_};
    string oldKernelPath = JoinPath({tmpLaunchDir, taskConfig.oldKernelName_});
    string newKernelPath = JoinPath({tmpLaunchDir, taskConfig.newKernelName_});
    if (!funcCtx->GetRegisterContext()->Save(oldKernelPath)) {
        DEBUG_LOG("Dump kernel object failed. stop running dbi task.");
        return nullptr;
    }
    uint64_t tilingKey{};
    string tilingKeyStr{};
    if (funcCtx->GetTilingKey(tilingKey)) {
        tilingKeyStr = to_string(tilingKey);
    }

    if (!Convert(config, oldKernelPath, newKernelPath, tilingKeyStr)) {
        DEBUG_LOG("Run dbi task failed, will use original kernel to run.");
        return nullptr;
    }
    auto newRegCtx = funcCtx->GetRegisterContext()->Clone(newKernelPath);
    if (!newRegCtx) {
        DEBUG_LOG("Replace handle failed: register binary failed.");
        return nullptr;
    }
    funcCtx_ = funcCtx->Clone(newRegCtx);
    if (!funcCtx_) {
        DEBUG_LOG("Replace handle failed: register function failed.");
        return nullptr;
    }
    launchId_ = launchId;
    return funcCtx_;
}

bool DBITask::Run(void **handle, uint64_t launchId, bool withStubFunc)
{
    uint64_t regId = KernelContext::Instance().GetRegisterId(launchId);
    if (ReuseConverted(handle, launchId, regId)) {
        return true;
    }
    KernelContext::LaunchEvent event;
    if (!KernelContext::Instance().GetLaunchEvent(launchId, event)) {
        ERROR_LOG("Get launch event failed.");
        return false;
    };
    const auto &taskConfig = DBITaskConfig::Instance();
    string tmpLaunchDir = taskConfig.GetOutputDir(launchId);
    if (!MkdirRecusively(tmpLaunchDir)) {
        ERROR_LOG("Mkdir tmp launch dir failed, stop running dbi task.");
        return false;
    }
    shared_ptr<void> defer0(nullptr, [&tmpLaunchDir](std::nullptr_t&) {
        if (!DBITaskConfig::Instance().keepTaskOutputs_) {
            RemoveAll(tmpLaunchDir);
        }
    });
    BinaryInstrumentation::Config config{taskConfig.pluginPath_, GetCurrentArchName(), tmpLaunchDir,
        taskConfig.tuneLogPath_, taskConfig.argsSize_, taskConfig.extraCompilerArgs_};
    string oldKernelPath = JoinPath({tmpLaunchDir, taskConfig.oldKernelName_});
    string newKernelPath = JoinPath({tmpLaunchDir, taskConfig.newKernelName_});
    if (!KernelContext::Instance().DumpKernelObject(regId, oldKernelPath)) {
        DEBUG_LOG("Dump kernel object failed. stop running dbi task.");
        return false;
    }
    string tilingKey{};
    if (!event.stubFunc) { tilingKey = to_string(event.tilingKey);}

    if (!Convert(config, oldKernelPath, newKernelPath, tilingKey)) {
        DEBUG_LOG("Run dbi task failed, will use original kernel to run.");
        return false;
    }
    replaceTask_.reset(new KernelReplaceTask(newKernelPath));
    if (replaceTask_->Run(handle, regId, withStubFunc)) {
        launchId_ = launchId;
        // update stubBin field in registerEvent
        KernelContext::Instance().SetDbiBinary(regId, replaceTask_->GetDevBinary());
        return true;
    }
    DEBUG_LOG("Replace handle failed.");
    replaceTask_.reset();
    return false;
}

bool DBITask::Run(void **handle, const void **stubFunc, uint64_t launchId)
{
    // replace binary handle
    if (!DBITask::Run(handle, launchId, true)) {
        return false;
    }
    if (registered_) {
        *stubFunc = &this->stubFunc_;
        return true;
    }

    KernelContext::LaunchEvent launchEvent;
    if (!KernelContext::Instance().GetLaunchEvent(launchId, launchEvent)) {
        ERROR_LOG("Get launch event failed.");
        return false;
    };

    KernelContext::StubFuncInfo stubFuncInfo;
    if (!KernelContext::Instance().GetStubFuncInfo(KernelContext::StubFuncPtr{launchEvent.stubFunc}, stubFuncInfo)) {
        ERROR_LOG("Get stubFuncInfo failed");
        return false;
    }

    rtError_t ret = rtFunctionRegisterOrigin(*handle, &this->stubFunc_,
                                             stubFuncInfo.kernelName.c_str(),
                                             stubFuncInfo.kernelInfoExt.c_str(), stubFuncInfo.funcMode);
    if (ret != RT_ERROR_NONE) {
        ERROR_LOG("register function for stubName %s failed. error code: %d", stubFuncInfo.kernelName.c_str(), ret);
        return false;
    }
    registered_ = true;
    *stubFunc = &this->stubFunc_;
    return true;
}

bool DBITask::Convert(const BinaryInstrumentation::Config &config, const string &oldKernelPath,
                      const string &newKernelPath, const string &tilingKey)
{
    if (!dbi_->SetConfig(config)) {
        return false;
    }
    auto start = std::chrono::system_clock::now();
    if (!dbi_->Convert(newKernelPath, oldKernelPath, tilingKey)) {
        return false;
    }
    auto end = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    DEBUG_LOG("Run dbi task success. time duration: %fs", duration.count() / 1000.f);
    return true;
}

DBITaskConfig &DBITaskConfig::Instance()
{
    thread_local static DBITaskConfig inst;
    return inst;
}

void DBITaskConfig::Init(BIType type, const std::string &pluginPath, const KernelMatcher::Config &config,
    const std::string &tmpPath, const std::string &tuneLogPath, const std::vector<std::string> &extraCompilerArgs) {
    enabled_ = true;
    pluginPath_ = pluginPath;
    type_ = type;
    matcher_ = MakeShared<KernelMatcher>(config);
    tuneLogPath_ = tuneLogPath;
    extraCompilerArgs_ = extraCompilerArgs;
    SetTmpRootDir(tmpPath);
}

void DBITaskConfig::Init(BIType type, const std::shared_ptr<KernelMatcher> &matcher, const std::string &tmpPath)
{
    enabled_ = true;
    pluginPath_ = "";
    type_ = type;
    matcher_ = matcher;
    SetTmpRootDir(tmpPath);
}

DBITaskConfig::DBITaskConfig()
{
    keepTaskOutputs_ = (InjectLogger::Instance().GetLogLv() <= LogLv::DEBUG);
    keepRootTmpDirOutputs_ = keepTaskOutputs_;
}

DBITaskConfig::~DBITaskConfig()
{
    if (!keepRootTmpDirOutputs_ && IsExist(tmpRootDir_)) {
        RemoveAll(tmpRootDir_);
    }
}

bool DBITaskConfig::IsEnabled(uint64_t launchId, const string &kernelName) const
{
    if (!enabled_) {
        DEBUG_LOG("not enable dbi task");
        return false;
    }
    if (!matcher_ || !matcher_->Match(launchId, kernelName)) {
        DEBUG_LOG("Can not match kernel name %s", kernelName.c_str());
        return false;
    }
    return true;
}

void DBITaskConfig::SetTmpRootDir(const std::string &path)
{
    if (!tmpRootDir_.empty()) {
        return;
    }
    if (path.empty()) {
        tmpRootDir_ = "./tmp_" +  std::to_string(getpid()) + "_" + GenerateTimeStamp<std::chrono::nanoseconds>();
        return;
    }
    tmpRootDir_ = path + "/tmp_" +  std::to_string(getpid()) + "_" + GenerateTimeStamp<std::chrono::nanoseconds>();
}

uint8_t* InitMemory(uint64_t memSize)
{
    if (memSize == 0) {
        return nullptr;
    }
    DEBUG_LOG("Dbi task init with memSize: %lu", memSize);
    void *memInfo;
    aclError ret = DevMemManager::Instance().MallocMemory(&memInfo, memSize);
    if (ret != ACL_ERROR_NONE) {
        ERROR_LOG("malloc memInfo error: %d", ret);
        return nullptr;
    }
    aclError error = aclrtMemsetImplOrigin(memInfo, memSize, 0x00, memSize);
    if (error != ACL_ERROR_NONE) {
        ERROR_LOG("init rtMemset memInfo error: %d", error);
        return nullptr;
    }
    return static_cast<uint8_t *>(memInfo);
}

bool RunDBITask(const StubFunc **stubFunc)
{
    if (!IsPlatformSupportDBI()) {
        DEBUG_LOG("Unsupported platform, exit DBI");
        return false;
    }
    void const *lastStubFunc = *stubFunc;
    rtDevBinary_t binary;
    if (!KernelContext::Instance().GetDevBinary(KernelContext::StubFuncPtr{*stubFunc}, binary)) {
        ERROR_LOG("get binary failed");
        return false;
    }
    uint64_t launchId = KernelContext::Instance().GetLaunchId();
    string kernelName = KernelContext::Instance().GetLaunchName();
    const auto &config = DBITaskConfig::Instance();
    if ((!config.IsEnabled(launchId, kernelName)) || HasStaticStub(binary)) {
        return false;
    }
    uint64_t regId = KernelContext::Instance().GetRegisterId(launchId);
    KernelContext::LaunchEvent launchEvent;
    if (!KernelContext::Instance().GetLaunchEvent(launchId, launchEvent)) {
        ERROR_LOG("get launch event by launchId %lu failed", launchId);
        return false;
    }
    DEBUG_LOG("DBI with launch id = %lu, kernelName=%s", launchId, kernelName.c_str());
    KernelHandle *hdl{nullptr};
    auto task = DBITaskFactory::Instance().GetOrCreate(regId, *stubFunc, config.type_, config.pluginPath_);
    if (!task || !task->Run(&hdl, stubFunc, launchId)) {
        return false;
    }
    return KernelContext::Instance().GetDeviceContext()
        .UpdatePcStartAddrByDbi(launchId, KernelContext::StubFuncArgs{lastStubFunc, *stubFunc});
}

bool RunDBITask(void **hdl, const uint64_t tilingKey)
{
    if (!IsPlatformSupportDBI()) {
        DEBUG_LOG("Unsupported platform, exit DBI");
        return false;
    }
    void const *lastHdl = *hdl;
    rtDevBinary_t binary;
    if (!KernelContext::Instance().GetDevBinary(KernelContext::KernelHandlePtr{*hdl}, binary)) {
        ERROR_LOG("get binary failed");
        return false;
    }
    uint64_t launchId = KernelContext::Instance().GetLaunchId();
    string kernelName = KernelContext::Instance().GetLaunchName();
    const auto &config = DBITaskConfig::Instance();
    if ((!config.IsEnabled(launchId, kernelName)) || HasStaticStub(binary)) {
        return false;
    }
    uint64_t regId = KernelContext::Instance().GetRegisterId(launchId);
    DEBUG_LOG("DBI with launch id = %lu, kernelName=%s", launchId, kernelName.c_str());
    auto task = DBITaskFactory::Instance().GetOrCreate(regId, to_string(tilingKey),
        config.type_, config.pluginPath_);
    if (!task || !task->Run(hdl, launchId)) {
        return false;
    }
    return KernelContext::Instance().GetDeviceContext()
        .UpdatePcStartAddrByDbi(launchId, KernelContext::KernelHandleArgs{lastHdl, *hdl, tilingKey});
}

FuncContextSP RunDBITask(const LaunchContextSP &launchCtx)
{
    if (!IsPlatformSupportDBI()) {
        DEBUG_LOG("Unsupported platform, exit DBI");
        return nullptr;
    }
    uint64_t launchId = launchCtx->GetLaunchId();
    string kernelName = launchCtx->GetFuncContext()->GetKernelName();
    const auto &config = DBITaskConfig::Instance();
    if ((!config.IsEnabled(launchId, kernelName)) || launchCtx->GetFuncContext()->GetRegisterContext()->HasStaticStub()) {
        return nullptr;
    }
    DEBUG_LOG("DBI with launch id = %lu, kernelName=%s", launchId, kernelName.c_str());
    uint64_t regId = launchCtx->GetFuncContext()->GetRegisterContext()->GetRegisterId();
    // 暂时取巧使用了kernelName替代tilingKey，反正两个东西具有相同的唯一性
    auto task = DBITaskFactory::Instance().GetOrCreate(regId, kernelName, config.type_, config.pluginPath_);
    if (task == nullptr) {
        return nullptr;
    }
    auto newFuncCtx = task->Run(launchCtx);
    return newFuncCtx;
}
