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


#ifndef FUNC_INJECTION_CAMODEL_H
#define FUNC_INJECTION_CAMODEL_H
#include <string>
#include "dvc_log_api.h"
constexpr int CAMOLDEL_ERROR_INTERNAL_ERROR = 100000; // 100000 used when func ptr was nullptr
void CamodelCtor();
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
    char decodeDescr[200];
    char execDescr[200];
};

struct DvcCcuLog {
    uint64_t time;
    uint64_t pc;
    uint32_t coreId;
    uint32_t subCoreId;
};

int DvcSetLogLevelOrigin(const uint32_t filePrintLevel, const uint32_t screenPrintLevel, const uint32_t flushLevel);
int DvcAttachLogCallbackOrigin(DvcLogType_t logType, DvcLogCbFnUnion_t fnUnion);

#endif // FUNC_INJECTION_CAMODEL_H
