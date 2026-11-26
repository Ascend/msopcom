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

HijackedFuncOfDisableP2P::HijackedFuncOfDisableP2P() : HijackedFuncType(RuntimeLibName(), "rtDisableP2P") {}

void HijackedFuncOfDisableP2P::Pre(uint32_t deviceId, uint32_t peerPhyId) {
    deviceId_ = deviceId;
    peerPhyId_ = peerPhyId;
}

rtError_t HijackedFuncOfDisableP2P::Post(rtError_t ret) {
    if (ret != RT_ERROR_NONE || !IsSanitizer()) {
        return ret;
    }

    int32_t peerUserDeviceId = -1;
    // 撤权使用与开通和内存登记相同的进程内设备号
    if (aclrtGetUserDevIdByPhyDevIdOrigin(static_cast<int32_t>(peerPhyId_), &peerUserDeviceId) != ACL_SUCCESS ||
        peerUserDeviceId < 0) {
        return ret;
    }

    const int32_t deviceId = static_cast<int32_t>(deviceId_);
    MemoryManage::RevokeP2PMappings(deviceId, peerUserDeviceId);
    PeerAccessRecord record{deviceId, peerUserDeviceId, PeerAccessOp::DISABLE};
    PeerAccessState::Instance().Apply(deviceId, peerUserDeviceId, PeerAccessOp::DISABLE);
    InteractHelper::Notify(deviceId, PacketType::PEER_ACCESS_RECORD, record);
    return ret;
}
