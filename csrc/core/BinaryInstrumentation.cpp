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


#include "BinaryInstrumentation.h"
#include <iterator>
#include <vector>
#include <dlfcn.h>

#include "core/FunctionLoader.h"
#include "core/PlatformConfig.h"
#include "utils/FileSystem.h"
#include "utils/PipeCall.h"
#include "utils/InjectLogger.h"
#include "utils/Future.h"
#include "utils/Ustring.h"
#include "utils/UmaskGuard.h"

using namespace std;
using namespace func_injection;

namespace {

constexpr mode_t DEFAULT_UMASK_FOR_LOG_FILE = 0027;

bool ParseFirstSymbol(std::string const &text, std::string const &property, std::string &symbol)
{
    std::vector<std::string> lines;
    SplitString(text, '\n', lines);

    constexpr std::size_t propertyIndex = 1;
    constexpr std::size_t typeIndex = 2;
    constexpr std::size_t sectionIndex = 3;
    constexpr std::size_t symbolIndex = 5;
    for (auto const &line: lines) {
        std::istringstream iss(line);
        auto items = std::vector<std::string>(std::istream_iterator<std::string>{iss},
                                              std::istream_iterator<std::string>{});
        if (items.size() > symbolIndex && items[propertyIndex] == property &&
            items[typeIndex] == "F" && items[sectionIndex] == ".text") {
            symbol = items[symbolIndex];
            return true;
        }
    }
    return false;
}

} // namespace [Dummy]

BinaryInstrumentation::BinaryInstrumentation(BIType biType): config_{}, biType_(biType) {}

bool BBCountDBI::Convert(
    const std::string &newKernelFile, const std::string &oldKernelFile, const std::string &tilingKey) {
    UmaskGuard guard{DEFAULT_UMASK_FOR_LOG_FILE};
    std::vector<std::string> args;
    if (config_.archName.find("dav-c310") != std::string::npos) {
        args = {
            "bisheng-tune",
            "--action=instru-probe",
            "--tune-bbbend-offset",
            "--instru-bbprobe",
            // argSize=0 is invalid,bisheng-tune will calculate by self.
            "--tune-argsize=" + std::to_string(config_.argSize),
            oldKernelFile,
            "-o",
            newKernelFile
        };
    } else {
        args = {
            "bisheng-tune",
            "--action=block-count-instr",
            "--tune-bbbend-offset",
            "--tune-argsize=" + std::to_string(config_.argSize),
            oldKernelFile,
            "-o",
            newKernelFile
        };
    }

    if (!tilingKey.empty()) {
        args.push_back("--tiling-key=" + tilingKey);
    }
    std::string output;
    if (!PipeCall(args, output)) {
        WARN_LOG("Convert bbcount failed.");
        DEBUG_LOG("Command execute message: %.10240s", output.c_str());
        return false;
    }
    return Chmod(newKernelFile, SAVE_DATA_FILE_AUTHORITY);
}

bool PGODBI::Convert(const std::string &newKernelFile, const std::string &oldKernelFile, const std::string &tilingKey) {
    UmaskGuard guard{DEFAULT_UMASK_FOR_LOG_FILE};
    std::vector<std::string> args = {
        "bisheng-tune",
        "--action=block-count-instr",
        "--tune-argsize=" + std::to_string(config_.argSize),
        oldKernelFile,
        "-o=" + newKernelFile
    };
    if (!tilingKey.empty()) {
        args.push_back("--tiling-key=" + tilingKey);
    }
    std::string output;
    if (!PipeCall(args, output)) {
        return false;
    }
    return Chmod(newKernelFile, SAVE_DATA_FILE_AUTHORITY);
}

bool PGODBI::ExpandArgs(const std::string& expendArgs)
{
    return true;
}

bool CustomDBI::GenerateOrderingFile(std::string const &kernelFile,
                                     std::string const &probeFile,
                                     std::string const &orderingFile) const
{
    std::vector<std::string> args = { "llvm-objdump", "--syms", kernelFile };
    std::string output;
    if (!PipeCall(args, output)) {
        WARN_LOG("dump kernel symbols failed.");
        DEBUG_LOG("Command execute message: %.10240s", output.c_str());
        return false;
    }

    std::string kernelSymbol;
    if (!ParseFirstSymbol(output, "g", kernelSymbol)) {
        WARN_LOG("parse first kernel symbol failed");
        return false;
    }

    args = { "llvm-objdump", "--syms", probeFile };
    if (!PipeCall(args, output)) {
        WARN_LOG("dump probe symbols failed.");
        DEBUG_LOG("Command execute message: %.10240s", output.c_str());
        return false;
    }

    std::string probeSymbol;
    if (!ParseFirstSymbol(output, "w", probeSymbol)) {
        WARN_LOG("parse first probe symbol failed");
        return false;
    }

    std::string ordering = kernelSymbol + "\n" + probeSymbol;
    if (!WriteStringToFile(orderingFile, ordering)) {
        WARN_LOG("write ordering file failed");
        return false;
    }

    return true;
}

bool CustomDBI::GenerateTempProbe(const string &probeFile) const
{
    vector<string> args = {"llvm-objcopy", "-j", "." + config_.archName,
                           "-O", "binary", config_.pluginPath, probeFile,
    };
    string output;
    if (!PipeCall(args, output)) {
        WARN_LOG("Extract probe.o from plugin file failed.");
        return false;
    }
    return Chmod(probeFile, SAVE_DATA_FILE_AUTHORITY);
}

bool CustomDBI::GenerateKernelWithProbe(const string &kernelFile, const string &probeFile,
                                        const string &orderingFile, const string &kernelWithProbeFile) const
{
    vector<string> args = {"ld.lld", "-m", "aicorelinux", "-Ttext=0", "-execute-probe",
                           "--symbol-ordering-file", orderingFile,
                           "-z", "separate-loadable-segments", probeFile, kernelFile,
                           "-static", "-q", "-o", kernelWithProbeFile,
    };
    string output;
    if (!PipeCall(args, output)) {
        WARN_LOG("Generate kernel_with_probe file failed");
        DEBUG_LOG("Command execute message: %s", output.c_str());
        return false;
    }
    return Chmod(kernelWithProbeFile, SAVE_DATA_FILE_AUTHORITY);
}

bool CustomDBI::Convert(
    const std::string &newKernelFile, const std::string &oldKernelFile, const std::string &tilingKey) {
    const string &archName = config_.archName;
    if (!initFunc_ || archName.empty()) {
        return false;
    }
    string tempProbePath = JoinPath({config_.tmpDir, "probe.o"});
    string tempCtrlPath = JoinPath({config_.tmpDir, "ctrl.bin"});
    string tempOrderingPath = JoinPath({config_.tmpDir, "symbol_ordering.txt"});
    string tempKernelWithProbePath = JoinPath({config_.tmpDir, "old_kernel_with_probe.o"});

    if (!GenerateTempProbe(tempProbePath)) {
        WARN_LOG("generate probe.o failed.");
        return false;
    }

    if (!GenerateOrderingFile(oldKernelFile, tempProbePath, tempOrderingPath)) {
        WARN_LOG("generate link ordering file failed");
        return false;
    }

    if (!GenerateKernelWithProbe(oldKernelFile, tempProbePath, tempOrderingPath, tempKernelWithProbePath)) {
        WARN_LOG("generate kernel with probe file failed");
        return false;
    }
    initFunc_(tempCtrlPath.c_str(), tempCtrlPath.length(), archName.c_str());
    if (!Chmod(tempCtrlPath, SAVE_DATA_FILE_AUTHORITY)) {
        return false;
    }
    string output;
    vector<string> args = {
        "bisheng-tune", "--action=instru-probe", "--tune-argsize=" + to_string(config_.argSize), "--instru-memprobe",
        tempKernelWithProbePath, "--dbi-config=" + tempCtrlPath, "-o=" + newKernelFile
    };
    for (size_t i = 0; i < config_.extraArgs.size(); ++i) {
        args.push_back(config_.extraArgs[i]);
    }
    if (!tilingKey.empty()) {
        args.push_back("--tiling-key=" + tilingKey);
    }
    auto finalize = std::shared_ptr<void>(nullptr, [&](void*) {
        FinalizeOutput(output);
    });
    if (!PipeCall(args, output)) {
        WARN_LOG("Generate kernel from tune failed.");
        DEBUG_LOG("Command execute message: %s", output.c_str());
        return false;
    }
    return Chmod(newKernelFile, SAVE_DATA_FILE_AUTHORITY);
}

void CustomDBI::FinalizeOutput(const std::string &output) const
{
    // 保存输出日志，修改输出件的权限
    if (!config_.tuneLogPath.empty()) {
        WriteStringToFile(config_.tuneLogPath, output);
    }
    for (const auto &arg : config_.extraArgs) {
        const std::string prefix = "--dfx-region-id-map=";
        if (arg.find(prefix) == 0) {
            std::string dfxMapPath = arg.substr(prefix.length());
            if (IsExist(dfxMapPath)) {
                Chmod(dfxMapPath, SAVE_DATA_FILE_AUTHORITY);
            }
            break;
        }
    }
}

bool CustomDBI::SetConfig(const Config& config)
{
    config_ = config;
    const string &pluginPath = config.pluginPath;
    if (pluginPath.empty() || config.archName.empty()) {
        DEBUG_LOG("Invalid dbi config, empty plugin path (%s) or empty arch name (%s).",
            pluginPath.c_str(),
            config.archName.c_str());
        return false;
    }
    if (!CheckInputFileValid(pluginPath, "so")) {
        DEBUG_LOG("Invalid dbi config, plugin path check failed");
        return false;
    }
    handle_ = dlopen(pluginPath.c_str(), RTLD_LAZY);
    if (handle_ == nullptr) {
        DEBUG_LOG("Invalid dbi config, dlopen %s failed", pluginPath.c_str());
        return false;
    }
    initFunc_ = reinterpret_cast<PluginInitFunc>(dlsym(handle_, "MSBitStart"));
    if (!initFunc_) {
        DEBUG_LOG("Invalid dbi config, no msbitStart function");
        return false;
    }
    return true;
}

CustomDBI::~CustomDBI()
{
    if (handle_) {
        dlclose(handle_);
    }
}

DBIFactory &DBIFactory::Instance()
{
    static DBIFactory inst;
    return inst;
}

BinaryInstrumentationSP DBIFactory::Create(BIType type) const
{
    switch (type) {
        case BIType::PGO:
            return MakeShared<PGODBI>();
        case BIType::BB_COUNT:
            return MakeShared<BBCountDBI>();
        case BIType::CUSTOMIZE:
            return MakeShared<CustomDBI>();
        default:
            return nullptr;
    }
}
