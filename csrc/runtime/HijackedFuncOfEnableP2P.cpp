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

#include "HijackedFunc.h"
#include "RuntimeConfig.h"
#include "ascendcl/AscendclOrigin.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/InteractHelper.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "utils/Protocol.h"

HijackedFuncOfEnableP2P::HijackedFuncOfEnableP2P() : HijackedFuncType(RuntimeLibName(), "rtEnableP2P") {}

void HijackedFuncOfEnableP2P::Pre(uint32_t deviceId, uint32_t peerPhyId, uint32_t) {
    deviceId_ = deviceId;
    peerPhyId_ = peerPhyId;
}

rtError_t HijackedFuncOfEnableP2P::Post(rtError_t ret) {
    if (ret != RT_ERROR_NONE || !IsSanitizer()) {
        return ret;
    }

    int32_t peerUserDeviceId = -1;
    // 该接口直接返回进程内设备号，与本端入参和分配记录使用同一套编号
    if (aclrtGetUserDevIdByPhyDevIdOrigin(static_cast<int32_t>(peerPhyId_), &peerUserDeviceId) != ACL_SUCCESS ||
        peerUserDeviceId < 0) {
        return ret;
    }

    const int32_t deviceId = static_cast<int32_t>(deviceId_);
    PeerAccessRecord record{deviceId, peerUserDeviceId, PeerAccessOp::ENABLE};
    PeerAccessState::Instance().Apply(deviceId, peerUserDeviceId, PeerAccessOp::ENABLE);
    InteractHelper::Notify(deviceId, PacketType::PEER_ACCESS_RECORD, record);
    return ret;
}
