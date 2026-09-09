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

#ifndef FUNC_INJECTION_CAMODEL_RAW_INSTR_DUMPER_H
#define FUNC_INJECTION_CAMODEL_RAW_INSTR_DUMPER_H

#include <cstdint>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

#include "include/opprof/CamodelLogProtocol.h"

enum class RawInstrCallbackType : uint8_t {
    POPPED = 0,
    COMPLETE,
};

class CamodelRawInstrDumper {
public:
    static CamodelRawInstrDumper &Instance();

    bool Start(const std::string &outputDir);
    void Record(RawInstrCallbackType type, const Common::DvcInstrLogV2 &entry);
    void Stop();

private:
    CamodelRawInstrDumper() = default;
    ~CamodelRawInstrDumper();
    CamodelRawInstrDumper(const CamodelRawInstrDumper &) = delete;
    CamodelRawInstrDumper &operator=(const CamodelRawInstrDumper &) = delete;

    bool EnsureCoreFilesLocked(uint32_t coreId, const std::string &subCoreName);
    bool OpenOutputLocked(const std::string &outputPath);
    void StopLocked();
    void WarnWriteFailureLocked(const char *reason);

    std::mutex mutex_;
    std::map<std::string, std::ofstream> outputs_;
    std::string outputDir_;
    uint32_t warningCount_{0};
    bool enabled_{false};
};

#endif // FUNC_INJECTION_CAMODEL_RAW_INSTR_DUMPER_H
