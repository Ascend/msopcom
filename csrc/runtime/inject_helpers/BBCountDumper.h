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

#ifndef __RUNTIME_INJECT_HELPERS_BBCOUNT_DUMPER_H__
#define __RUNTIME_INJECT_HELPERS_BBCOUNT_DUMPER_H__

#include <string>
#include <vector>
#include <memory>

#include "runtime.h"
#include "runtime/inject_helpers/KernelMatcher.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/LaunchContext.h"
#include "runtime/inject_helpers/FuncContext.h"
#include "core/BinaryInstrumentation.h"
#include "utils/Future.h"


class BBCountDumper {
public:
    static BBCountDumper &Instance()
    {
        static BBCountDumper inst;
        return inst;
    }


    void Init(const std::string &outputDir, const KernelMatcher::Config &config,
        const BIType type = BIType::BB_COUNT)
    {
        enabled_ = true;
        outputDir_ = outputDir;
        matcher_ = MakeShared<KernelMatcher>(config);
        type_ = type;
    }

    bool IsEnabled() const { return enabled_; }

    std::string GetOutputDir();

    bool DumpBBData(uint64_t regId, uint64_t memSize, uint8_t *memInfo);

    std::string GenExtraAndReturnName(const std::string &path, uint64_t regId, uint64_t memSize, uint8_t *memInfo);

    bool Replace(void **hdl, const uint64_t tilingKey, uint64_t launchId, const std::string &outputPath = "");

    bool Replace(const StubFunc **stubFunc, uint64_t launchId, const std::string &outputPath = "");

    FuncContextSP Replace(const LaunchContextSP &launchCtx, const std::string &outputPath);

    uint64_t GetMemSize(uint64_t regId, const std::string& outputPath = "");

    // used only ut test
    void Reset()
    {
        enabled_ = false;
        outputDir_ = "";
        matcher_ = nullptr;
        stubDataList_ = {};
        devBinaryList_ = {};
        lastLaunchId_ = {};
        type_ = BIType::MAX;
    }

    bool GetLaunchCount(const uint64_t regId, uint64_t &count) const;

private:
    BBCountDumper() = default;
    ~BBCountDumper() = default;
    void SaveBlockMapFile(uint64_t launchId, uint64_t regId, const std::string &outputPath = "");

    void InitDBITaskConfig(uint64_t regId, const std::string &outputPath) const;

    enum class DataType {
        KERNEL = 0, // kernel bin文件，当kernel 注册时会分配一个kernelID与一个kernel指针对应
        HDL,        // kernel的handle文件
        STUBKERNEL, // kernel对应的stub文件
        STUBFUNC,   // 在kernel launch中，通过入参 stubFunc找到当前launch的kernelID
        EXTRAMEM,   // 为了存储板上信息而分配的内存地址与kernelID的对应关系
        BLOCKMAP,  // 获取bbcount map文件
        INVALID,
    };
    // remove in the future, path should save in config
    std::string GetOutFileName(DataType type, uint64_t regId) const;

private:
    bool enabled_{false};
    std::string outputDir_;
    std::shared_ptr<KernelMatcher> matcher_;
    std::vector<std::vector<char>> stubDataList_;
    std::vector<rtDevBinary_t> devBinaryList_;
    std::unordered_map<uint64_t, uint64_t> lastLaunchId_;
    BIType type_;
    std::mutex lastLaunchIdMapMtx_;
};

#endif // __RUNTIME_INJECT_HELPERS_BBCOUNT_DUMPER_H__
