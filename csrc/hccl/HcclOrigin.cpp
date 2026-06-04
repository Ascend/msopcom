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

#include "HcclOrigin.h"
#include "core/FunctionLoader.h"

#define LOAD_FUNCTION_BODY(soName, funcName, ...) FUNC_BODY(soName, funcName, Origin, HCCL_E_INTERNAL, __VA_ARGS__)

void HcclOriginCtor() {
    REGISTER_LIBRARY("hccl");
    REGISTER_FUNCTION("hccl", HcclBarrier);
}

HcclResult HcclBarrierOrigin(HcclComm comm, aclrtStream stream) {
    LOAD_FUNCTION_BODY("hccl", HcclBarrier, comm, stream);
}
