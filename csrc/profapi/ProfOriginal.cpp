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

#include "ProfOriginal.h"

#define LOAD_FUNCTION_BODY(soName, funcName, ...) \
    FUNC_BODY(soName, funcName, Origin, PROF_ERROR_INTERNAL_ERROR, __VA_ARGS__)

int32_t profSetProfCommandOrigin(VOID_PTR command, uint32_t len) {
    LOAD_FUNCTION_BODY("profapi", profSetProfCommand, command, len);
}

int32_t MsprofRegisterProfileCallbackOrigin(uint32_t callbackType, VOID_PTR callback, uint32_t len) {
    LOAD_FUNCTION_BODY("profapi", MsprofRegisterProfileCallback, callbackType, callback, len);
}
