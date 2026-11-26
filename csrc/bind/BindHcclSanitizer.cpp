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

#include "hccl/HcclOrigin.h"
#include "hccl/HcclWindowCollect.h"
#include "runtime/inject_helpers/ThreadContext.h"

extern "C" HcclResult HcclGetCommName(HcclComm comm, char *name) {
    const HcclResult ret = HcclGetCommNameOrigin(comm, name);
    if (ret == HCCL_SUCCESS) {
        // 业务在当前设备上获取已有通信域的名字；这里只记使用关系，窗口在kernel启动前查询
        RememberHcclComm(ThreadContext::Instance().GetDeviceId(), comm);
    }
    return ret;
}

extern "C" HcclResult HcclCommDestroy(HcclComm comm) { return DestroyHcclComm(comm); }
