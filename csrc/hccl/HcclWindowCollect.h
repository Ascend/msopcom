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

#pragma once

#include "hccl.h"

// GetCommName成功后记住设备与已有通信域的关系；重复调用保留历史，删除见DestroyHcclComm/ForgetHcclDevice
void RememberHcclComm(int32_t deviceId, HcclComm comm);
// 每次业务kernel启动前读取已建链窗口；未变化的窗口不重新授予已释放分配
void RefreshHcclWindows(int32_t deviceId);
// 查询与销毁串行，成功后撤销该域窗口并删除RememberHcclComm保存的使用关系，失败保留状态
HcclResult DestroyHcclComm(HcclComm comm);
// 设备reset后旧通信域句柄不可再查询；映射由设备reset统一清理
void ForgetHcclDevice(int32_t deviceId);
