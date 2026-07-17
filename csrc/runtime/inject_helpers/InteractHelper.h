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

#ifndef __INTERACT_HELPER__
#define __INTERACT_HELPER__
#include "utils/Protocol.h"
#include "utils/InjectLogger.h"
#include "utils/TypeTraits.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/ConfigManager.h"
#include "runtime/inject_helpers/ThreadContext.h"

PacketType QueryResponsePacketType(PacketType type);

class InteractHelper {
public:
    template <typename RecordT, typename RspT, uint32_t timeOut = 30>
    static inline bool Interact(const PacketType packetType, RecordT const &record, RspT &response) {
        // 先确保config已经接收过了
        DEBUG_LOG("Enter Interact, packetType: %u.", EnumToUnderlying(packetType));
        SanitizerConfigManager::Instance().GetConfig();
        int32_t deviceId = ThreadContext::Instance().GetDeviceId();
        Notify(deviceId, packetType, record);
        std::string msg;
        int readLen = LocalDevice::GetInstance(deviceId).Wait(msg, timeOut);
        if (readLen <= 0) {
            ERROR_LOG("Failed to receive response (packetType: %u, Timeout: %u seconds).",
                      EnumToUnderlying(packetType), timeOut);
            return false;
        }
        PacketType type;
        do {
            DEBUG_LOG("Read response, len: %lu. (packetType: %u)", msg.size(), EnumToUnderlying(packetType));
            if (!Deserialize(msg, type)) {
                ERROR_LOG("Failed to deserialize packet type (packetType: %u).", EnumToUnderlying(packetType));
                break;
            }
            PacketType expectType = QueryResponsePacketType(packetType);
            if (type != expectType) {
                ERROR_LOG("Error happened: mismatched response received (expect type:%u, received type:%u).",
                          EnumToUnderlying(expectType), EnumToUnderlying(type));
                break;
            }
            if (!Deserialize(msg.substr(sizeof(type)), response)) {
                ERROR_LOG("Failed to deserialize response type (packetType: %u).", EnumToUnderlying(packetType));
            }
            return true;
        } while (false);
        return false;
    }

    template <typename T>
    static inline void Notify(int32_t deviceId, PacketType type, const T &record)
    {
        PacketHead head = {type};
        LocalDevice::GetInstance(deviceId).Notify(Serialize(head, record));
    }
};

template <>
inline void InteractHelper::Notify(int32_t deviceId, PacketType type, const std::string &record)
{
        PacketHead head = {type};
        LocalDevice::GetInstance(deviceId).Notify(Serialize(head) + record);
}

// 用于几个IPC接口的参数和响应发送
void IPCInteract(const IPCMemRecord& record);

#endif
