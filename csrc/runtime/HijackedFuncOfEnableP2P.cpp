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
#include "RuntimeOrigin.h"
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

    int32_t logicDeviceId = -1;
    int32_t visiblePeerDeviceId = -1;
    // Hook 的本端设备号是用户设备号，使用显式入参，不依赖线程当前设备。
    if (rtsGetLogicDevIdByPhyDevIdOrigin(static_cast<int32_t>(peerPhyId_), &logicDeviceId) != RT_ERROR_NONE ||
        logicDeviceId < 0) {
        return ret;
    }
    if (rtGetVisibleDeviceIdByLogicDeviceIdOrigin(logicDeviceId, &visiblePeerDeviceId) != RT_ERROR_NONE ||
        visiblePeerDeviceId < 0) {
        return ret;
    }

    const int32_t deviceId = static_cast<int32_t>(deviceId_);
    PeerAccessRecord record{deviceId, visiblePeerDeviceId, PeerAccessOp::ENABLE};
    PeerAccessState::Instance().Apply(deviceId, visiblePeerDeviceId, PeerAccessOp::ENABLE);
    InteractHelper::Notify(deviceId, PacketType::PEER_ACCESS_RECORD, record);
    return ret;
}
