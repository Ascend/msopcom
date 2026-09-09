/* -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
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

#include "CamodelRawInstrDumper.h"

#include <iomanip>

#include "utils/FileSystem.h"
#include "utils/InjectLogger.h"

namespace {
constexpr uint32_t WRITE_WARNING_LIMIT = 10;

bool GetSubCoreName(uint32_t subCoreId, std::string &subCoreName) {
    const char *name = Common::GetDvcSubCoreName(subCoreId);
    if (name == nullptr) {
        return false;
    }
    subCoreName = name;
    return true;
}

std::string GetDumpFileName(uint32_t coreId, const std::string &subCoreName, RawInstrCallbackType type) {
    const char *logType = type == RawInstrCallbackType::POPPED ? "instr_popped_log" : "instr_log";
    return "core" + std::to_string(coreId) + "." + subCoreName + "." + logType + ".dump";
}

void WriteSingleLine(std::ofstream &output, const char *value) {
    if (value == nullptr) {
        return;
    }
    while (*value != '\0') {
        output << ((*value == '\n' || *value == '\r') ? ' ' : *value);
        ++value;
    }
}
}

CamodelRawInstrDumper &CamodelRawInstrDumper::Instance() {
    static CamodelRawInstrDumper instance;
    return instance;
}

CamodelRawInstrDumper::~CamodelRawInstrDumper() { Stop(); }

bool CamodelRawInstrDumper::Start(const std::string &outputDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    StopLocked();
    warningCount_ = 0;
    if (!IsDir(outputDir)) {
        WarnWriteFailureLocked("output directory does not exist");
        return false;
    }
    outputDir_ = outputDir;
    enabled_ = true;
    return true;
}

bool CamodelRawInstrDumper::OpenOutputLocked(const std::string &outputPath) {
    auto result = outputs_.emplace(outputPath, std::ofstream{});
    if (!result.second) {
        return result.first->second.is_open();
    }
    std::ofstream &output = result.first->second;
    output.open(outputPath, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        outputs_.erase(result.first);
        WarnWriteFailureLocked("open output file failed");
        return false;
    }
    if (!Chmod(outputPath, SAVE_DATA_FILE_AUTHORITY)) {
        output.close();
        outputs_.erase(outputPath);
        RemoveAll(outputPath);
        WarnWriteFailureLocked("set output file permission failed");
        return false;
    }
    return true;
}

bool CamodelRawInstrDumper::EnsureCoreFilesLocked(uint32_t coreId, const std::string &subCoreName) {
    const std::string completePath =
        JoinPath({outputDir_, GetDumpFileName(coreId, subCoreName, RawInstrCallbackType::COMPLETE)});
    const std::string poppedPath =
        JoinPath({outputDir_, GetDumpFileName(coreId, subCoreName, RawInstrCallbackType::POPPED)});
    return OpenOutputLocked(completePath) && OpenOutputLocked(poppedPath);
}

void CamodelRawInstrDumper::Record(RawInstrCallbackType type, const Common::DvcInstrLogV2 &entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_) {
        return;
    }
    if (entry.decodeDescr[0] == '\0') {
        WarnWriteFailureLocked("instruction callback content is empty");
        return;
    }
    std::string subCoreName;
    if (!GetSubCoreName(entry.subCoreId, subCoreName)) {
        WarnWriteFailureLocked("instruction callback sub-core id is invalid");
        return;
    }
    if (!EnsureCoreFilesLocked(entry.coreId, subCoreName)) {
        return;
    }

    const std::string outputPath = JoinPath({outputDir_, GetDumpFileName(entry.coreId, subCoreName, type)});
    auto iter = outputs_.find(outputPath);
    if (iter == outputs_.end() || !iter->second.is_open()) {
        return;
    }
    std::ofstream &output = iter->second;
    output << "[info] [" << std::setfill('0') << std::setw(8) << entry.time << "] ";
    WriteSingleLine(output, entry.decodeDescr);
    if (entry.extendParamsJson[0] != '\0') {
        output << "  ";
        WriteSingleLine(output, entry.extendParamsJson);
    }
    output << '\n';
    if (!output.good()) {
        WarnWriteFailureLocked("write output file failed");
        output.close();
        outputs_.erase(iter);
    }
}

void CamodelRawInstrDumper::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    StopLocked();
}

void CamodelRawInstrDumper::StopLocked() {
    enabled_ = false;
    for (auto &item : outputs_) {
        if (item.second.is_open()) {
            item.second.flush();
            item.second.close();
        }
    }
    outputs_.clear();
    outputDir_.clear();
}

void CamodelRawInstrDumper::WarnWriteFailureLocked(const char *reason) {
    if (warningCount_ >= WRITE_WARNING_LIMIT) {
        return;
    }
    ++warningCount_;
    if (warningCount_ == WRITE_WARNING_LIMIT) {
        WARN_LOG("Save Ascend950 instruction callback dump failed: %s. Further warnings will be suppressed", reason);
        return;
    }
    WARN_LOG("Save Ascend950 instruction callback dump failed: %s", reason);
}
