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

#pragma once
#include <string>
#include <memory>
#include <map>
#include <utility>

#include "core/BinaryInstrumentation.h"
#include "runtime/inject_helpers/KernelReplacement.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/FuncContext.h"
#include "runtime/inject_helpers/LaunchContext.h"
#include "utils/InjectLogger.h"
#include "utils/Ustring.h"

// 动态插桩的任务，执行动态插桩并管理动态插桩所需的资源申请与释放
class DBITask {
public:
    explicit DBITask(BIType type) : dbi_(DBIFactory::Instance().Create(type)) {}

    bool Run(void **handle, uint64_t launchId, bool withStubFunc = false);

    bool Run(void **handle, const void **stubFunc, uint64_t launchId);

    FuncContextSP Run(const LaunchContextSP &launchCtx);

    bool NeedConvert() const;

    bool Convert(const BinaryInstrumentation::Config &config,
                 const std::string &oldKernelPath,
                 const std::string &newKernelPath,
                 const std::string &tilingKey);

private:
    inline bool ReuseConverted(void **handle, uint64_t launchId, uint64_t registerId);
    inline bool ReuseConverted(uint64_t launchId) const;
private:
    BinaryInstrumentationSP dbi_;
    KernelReplaceTaskSP replaceTask_;
    uint64_t launchId_{0};
    uint64_t stubFunc_ {0};
    bool registered_{false};
    FuncContextSP funcCtx_{nullptr};
};

using DBITaskSP = std::shared_ptr<DBITask>;

struct TaskKey {
    BIType biType {BIType::MAX};
    const void *stubFunc {nullptr};
    std::string tilingKey;
    std::string pluginPath;
    TaskKey(BIType biType, const void * stubFunc, std::string tilingKey, std::string pluginPath) : biType(biType),
        stubFunc(stubFunc), tilingKey(std::move(tilingKey)), pluginPath(std::move(pluginPath)) {}
    bool operator<(const TaskKey &taskKey) const
    {
        if (biType != taskKey.biType) {
            return biType < taskKey.biType;
        }
        if (stubFunc != taskKey.stubFunc) {
            return stubFunc < taskKey.stubFunc;
        }
        if (tilingKey != taskKey.tilingKey) {
            return tilingKey < taskKey.tilingKey;
        }
        return pluginPath < taskKey.pluginPath;
    }
    bool operator==(const TaskKey &taskKey) const
    {
        return ((biType == taskKey.biType) && (stubFunc == taskKey.stubFunc) && (tilingKey == taskKey.tilingKey) &&
                (pluginPath != taskKey.pluginPath));
    }
};

// 动态插桩的任务创建与销毁,对于同一个kernel, tilingKey,
// 只保留一个插桩任务,避免冗余插桩行为
class DBITaskFactory {
public:
    static DBITaskFactory &Instance();

    DBITaskSP GetOrCreate(uint64_t regId, const std::string &tilingKey, BIType type,
                          const std::string &pluginPath = "");

    DBITaskSP GetOrCreate(uint64_t regId, const void *stubFunc, BIType type, const std::string &pluginPath = "");

    void Destroy(uint64_t regId);

    // used only by ut test
    void Reset();

private:
    // TaskKey后续应该使用kernelName即可，regId+kernelName确定一个kernelFunction
    std::map<uint64_t, std::map<TaskKey, DBITaskSP>> taskPool_;
};

// 设置是否开启动态插桩以及动态插桩开启时所需的配置
class DBITaskConfig {
public:
    static DBITaskConfig &Instance();

    void Init(BIType type, const std::string &pluginPath = "", const KernelMatcher::Config &config = {},
        const std::string &tmpPath = "", const std::string &tuneLogPath = "", const std::vector<std::string> &extraCompilerArgs = {});
    void Init(BIType type, const std::shared_ptr<KernelMatcher> &matcher, const std::string &tmpPath = "");

    bool IsEnabled(uint64_t launchId, const std::string &kernelName) const;
    // used only by ut test
    void Reset() const;

    void KeepTaskOutputs() { keepTaskOutputs_ = true; }

    void SetTmpRootDir(const std::string &path);

    // 用于外部获取相关动态插桩中间产物
    std::string GetOutputDir(uint64_t launchId) const
    {
        std::string biTypeName {};
        auto iter = BI_TYPE_NAME.find(type_);
        if (iter != BI_TYPE_NAME.end()) {
            biTypeName = iter->second;
        }
        std::string soName = "default";
        soName = ExtractObjName(pluginPath_, "plugin_", ".so");
        return JoinPath({tmpRootDir_, "launch_" + std::to_string(launchId) + "_" + biTypeName + "_" + soName});
    }

    ~DBITaskConfig();

public:
    std::vector<std::string> extraCompilerArgs_{};
    std::string tuneLogPath_{};
    std::string oldKernelName_{"tmp_old_kernel.o"};
    std::string newKernelName_{"tmp_new_kernel.o"};
    std::string tmpRootDir_;
    std::string pluginPath_;
    BIType type_{BIType::MAX};
    uint32_t argsSize_{0};
    bool keepTaskOutputs_{false};
    bool keepRootTmpDirOutputs_{false};

private:
    DBITaskConfig();
    bool enabled_{false};
    std::shared_ptr<KernelMatcher> matcher_;

public:
    DBITaskConfig(const DBITaskConfig&) = delete;
    DBITaskConfig(DBITaskConfig &&) = delete;
    DBITaskConfig& operator=(DBITaskConfig const&) & = delete;
};

// call dbi task when kernel launch
bool RunDBITask(const StubFunc **stubFunc);
bool RunDBITask(void **hdl, const uint64_t tilingKey);
FuncContextSP RunDBITask(const LaunchContextSP &launchCtx);
uint8_t* InitMemory(uint64_t memSize);

inline bool IsPlatformSupportDBI() // for dbi task
{
    const std::string &socVersion = DeviceContext::Local().GetSocVersion();
    return socVersion.find("Ascend910B") != std::string::npos ||
           socVersion.find("Ascend910_93") != std::string::npos ||
           socVersion.find("Ascend950") != std::string::npos;
}
