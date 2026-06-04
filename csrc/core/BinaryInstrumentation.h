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


#ifndef __BINARY_INSTRUCTION__
#define __BINARY_INSTRUCTION__

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <map>

constexpr char const *CUSTOMIZE  = "customize";
constexpr char const *BB_COUNT = "bbcount";
constexpr char const *PGO = "pgo";

enum class BIType : uint8_t {
    CUSTOMIZE,  // 自定义能力，用户提供插件
    BB_COUNT,   // 内置：bb块计数能力
    PGO,        // 内置：Profile-Guided Optimization 根据运行时profile数据对代码块进行重排优化
    MAX         // 空
};

const std::map<BIType, std::string> BI_TYPE_NAME = {
    { BIType::CUSTOMIZE,  CUSTOMIZE},
    { BIType::BB_COUNT,   BB_COUNT},
    { BIType::PGO,        PGO},
};

// BinaryInstrumentation类主要实现对kernel的二进制插桩，输入原始kernel，输出插桩后的kernel。
// 在现有功能下，调用方需要明确此次插桩功能，主要分为2种：
// 1. 内置的插桩对象
// 2. 自定义二进制插件
// 该类仅完成插桩，需要调用方自己准备好输入的kernel，并把新kernel应用。
// 对于打桩的后的生成的内容的
class BinaryInstrumentation {
public:
    struct Config {
        std::string pluginPath;
        std::string archName; // e.g dav-m200
        std::string tmpDir;
        std::string tuneLogPath; // bisheng-tune日志输出目录，为空不落盘
        uint32_t argSize;
        std::vector<std::string> extraArgs;
    };

    explicit BinaryInstrumentation(BIType biType);
    virtual ~BinaryInstrumentation() = default;
    // 由于跨进程所以此处暂时没必要提供内存的方法
    virtual bool Convert(const std::string& newKernelFile, const std::string& oldKernelFile,
        const std::string& tilingKey = "") = 0;
    virtual bool NeedExtraSpace() {return true;}
    // 部分DBI过程中，需要添加参数，该参数需要通过此处传递
    virtual bool ExpandArgs(const std::string& expendArgs) {return true;}
    // 部分DBI过程中，需要添加plugin so/archName等参数，该参数需要通过此处传递
    virtual bool SetConfig(const Config& config)
    {
        config_ = config;
        return true;
    }
protected:
    Config config_;
private:
    BIType biType_;
};

class BBCountDBI : public BinaryInstrumentation {
public:
    explicit BBCountDBI() : BinaryInstrumentation(BIType::BB_COUNT) {}
    ~BBCountDBI() override = default;
    bool Convert(const std::string& newKernelFile, const std::string& oldKernelFile,
        const std::string& tilingKey = "") override;
};

class PGODBI : public BinaryInstrumentation {
public:
    explicit PGODBI() : BinaryInstrumentation(BIType::PGO) {}
    ~PGODBI() override = default;
    bool Convert(const std::string& newKernelFile, const std::string& oldKernelFile,
        const std::string& tilingKey = "") override;
    bool ExpandArgs(const std::string& expendArgs) override;
    bool NeedExtraSpace() override {return false;}
};

class CustomDBI : public BinaryInstrumentation {
public:
    explicit CustomDBI() : BinaryInstrumentation(BIType::CUSTOMIZE) {}
    ~CustomDBI() override;
    bool Convert(const std::string& newKernelFile, const std::string& oldKernelFile,
        const std::string& tilingKey = "") override;
    bool SetConfig(const Config& config) override;

private:
    using PluginInitFunc = void (*)(const char *outputPath, uint16_t length, const char *archName);

    bool GenerateOrderingFile(std::string const &kernelFile,
                              std::string const &probeFile,
                              std::string const &orderingFile) const;
    bool GenerateTempProbe(std::string const &probeFile) const;
    bool GenerateKernelWithProbe(std::string const &kernelFile,
                                 std::string const &probeFile,
                                 std::string const &orderingFile,
                                 std::string const &kernelWithProbeFile) const;

    PluginInitFunc initFunc_{nullptr};
    void *handle_{nullptr};
};

using BinaryInstrumentationSP = std::shared_ptr<BinaryInstrumentation>;
class DBIFactory {
public:
    static DBIFactory &Instance();

    BinaryInstrumentationSP Create(BIType type) const;

private:
    DBIFactory() = default;
    DBIFactory(const DBIFactory&) = default;
    DBIFactory &operator=(const DBIFactory&)& = default;
};

#endif // __BINARY_INSTRUCTION__
