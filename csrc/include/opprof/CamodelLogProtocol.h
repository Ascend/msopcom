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

#ifndef MSOPPROF_CAMODEL_LOG_PROTOCOL_H
#define MSOPPROF_CAMODEL_LOG_PROTOCOL_H

#include <cstddef>
#include <cstdint>

namespace Common {
constexpr std::size_t DVC_INSTR_DECODE_DESCR_LENGTH = 200;
constexpr std::size_t DVC_EXTEND_PARAMS_JSON_LENGTH = 1024;

// subCoreId values carried by the Dvc*Log payloads: 0 is the cube core, 1 and 2 are the vector cores.
// Sender and receiver must resolve them through this single mapping.
inline const char *GetDvcSubCoreName(uint32_t subCoreId) {
    switch (subCoreId) {
    case 0:
        return "cubecore0";
    case 1:
        return "veccore0";
    case 2:
        return "veccore1";
    default:
        return nullptr;
    }
}

// POD payloads shared by the Camodel injection library and the msopprof receiver.
struct DvcMteLog {
    uint64_t time;
    uint64_t size;
    uint64_t instrId;
    uint32_t coreId;
    uint32_t reqId;
    char intf[32];
};

struct DvciCacheLog {
    uint64_t time;
    uint64_t addr;
    uint32_t coreId;
    uint32_t subCoreId;
    uint32_t size;
    uint32_t type;
    uint8_t last;
    char opType[32];
};

struct DvcInstrLog {
    uint64_t time;
    uint64_t pc;
    uint32_t coreId;
    uint32_t subCoreId;
    char decodeDescr[DVC_INSTR_DECODE_DESCR_LENGTH];
    char execDescr[200];
};

struct DvcInstrLogV2 {
    uint64_t time;
    uint64_t pc;
    uint32_t coreId;
    uint32_t subCoreId;
    // Base instruction fields are derived from decodeDescr by the receiver.
    char decodeDescr[DVC_INSTR_DECODE_DESCR_LENGTH];
    // Optional instruction details are transported in JSON form.
    char extendParamsJson[DVC_EXTEND_PARAMS_JSON_LENGTH];
};

struct DvcCcuLog {
    uint64_t time;
    uint64_t pc;
    uint32_t coreId;
    uint32_t subCoreId;
};
}

#endif // MSOPPROF_CAMODEL_LOG_PROTOCOL_H
