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

// FunctionLoader自身缓存并加锁；避免多线程首次查询时写未同步的函数指针。
#define LOAD_WINDOW_FUNCTION_BODY(soName, funcName, ...) \
    const auto function = reinterpret_cast<decltype(&funcName##Origin)>(GET_FUNCTION(soName, #funcName)); \
    return function == nullptr ? HCCL_E_INTERNAL : function(__VA_ARGS__)

void HcclOriginCtor() {
    REGISTER_LIBRARY("hccl");
    REGISTER_FUNCTION("hccl", HcclBarrier);
    REGISTER_FUNCTION("hccl", HcclGetCommName);
    REGISTER_FUNCTION("hccl", HcclCommDestroy);
    REGISTER_FUNCTION("hccl", HcclGetRankSize);
    REGISTER_LIBRARY("hcomm");
    REGISTER_FUNCTION("hcomm", CommGetRemoteCCLBuf);
}

HcclResult HcclBarrierOrigin(HcclComm comm, aclrtStream stream) {
    LOAD_FUNCTION_BODY("hccl", HcclBarrier, comm, stream);
}

HcclResult HcclGetCommNameOrigin(HcclComm comm, char *name) {
    LOAD_WINDOW_FUNCTION_BODY("hccl", HcclGetCommName, comm, name);
}

HcclResult HcclCommDestroyOrigin(HcclComm comm) { LOAD_WINDOW_FUNCTION_BODY("hccl", HcclCommDestroy, comm); }

HcclResult HcclGetRankSizeOrigin(HcclComm comm, uint32_t *rankSize) {
    LOAD_WINDOW_FUNCTION_BODY("hccl", HcclGetRankSize, comm, rankSize);
}

HcclResult CommGetRemoteCCLBufOrigin(HcclComm comm, uint32_t rank, void **addr, uint64_t *size) {
    LOAD_WINDOW_FUNCTION_BODY("hcomm", CommGetRemoteCCLBuf, comm, rank, addr, size);
}
