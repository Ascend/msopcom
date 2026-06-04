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

#include "AscendclOrigin.h"
#include "core/FunctionLoader.h"

#define LOAD_FUNCTION_BODY(soName, funcName, ...) \
    FUNC_BODY(soName, funcName, Origin, ACL_ERROR_INTERNAL_ERROR, __VA_ARGS__)

void AscendclOriginCtor() {
    REGISTER_LIBRARY("ascendcl");
    REGISTER_FUNCTION("ascendcl", aclrtSetOpExecuteTimeOut);
    REGISTER_FUNCTION("ascendcl", aclrtCreateEvent);
    REGISTER_FUNCTION("ascendcl", aclrtStreamWaitEvent);
    REGISTER_FUNCTION("ascendcl", aclrtRecordEvent);
    REGISTER_FUNCTION("ascendcl", aclrtResetEvent);
    REGISTER_FUNCTION("ascendcl", aclrtDestroyEvent);
}

aclError aclrtSetOpExecuteTimeOutOrigin(uint32_t timeout) {
    LOAD_FUNCTION_BODY("ascendcl", aclrtSetOpExecuteTimeOut, timeout);
}

aclError aclrtCreateEventOrigin(aclrtEvent *event) { LOAD_FUNCTION_BODY("ascendcl", aclrtCreateEvent, event); }

aclError aclrtStreamWaitEventOrigin(aclrtStream stream, aclrtEvent event) {
    LOAD_FUNCTION_BODY("ascendcl", aclrtStreamWaitEvent, stream, event);
}

aclError aclrtRecordEventOrigin(aclrtEvent event, aclrtStream stream) {
    LOAD_FUNCTION_BODY("ascendcl", aclrtRecordEvent, event, stream);
}

aclError aclrtResetEventOrigin(aclrtEvent event, aclrtStream stream) {
    LOAD_FUNCTION_BODY("ascendcl", aclrtResetEvent, event, stream);
}

aclError aclrtDestroyEventOrigin(aclrtEvent event) { LOAD_FUNCTION_BODY("ascendcl", aclrtDestroyEvent, event); }
