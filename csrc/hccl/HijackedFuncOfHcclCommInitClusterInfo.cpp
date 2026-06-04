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

// 该文件主要实现注入函数的功能，其配合被劫持函数的别名，实现新的劫持函数功能
#include "HijackedFunc.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "hccl.h"

HijackedFuncOfHcclCommInitClusterInfo::HijackedFuncOfHcclCommInitClusterInfo()
    : HijackedFuncType("hccl", "HcclCommInitClusterInfo") {}

void HijackedFuncOfHcclCommInitClusterInfo::Pre(const char *clusterInfo, uint32_t rank, HcclComm *comm) {
    this->comm_ = comm;
}

HcclResult HijackedFuncOfHcclCommInitClusterInfo::Post(HcclResult ret) {
    if (IsOpProf()) {
        if (this->comm_ != nullptr) {
            KernelContext::Instance().SetHcclComm(*this->comm_);
        }
    }
    return ret;
}
