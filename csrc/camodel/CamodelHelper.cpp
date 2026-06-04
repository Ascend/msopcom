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

#include <algorithm>

#include "utils/Serialize.h"
#include "Camodel.h"
#include "CamodelHelper.h"

void CamodelHelper::SendCaLog(std::unique_ptr<DataHolderBase> data) {
    std::lock_guard<std::mutex> lock(mtx_);
    queue_.push(std::move(data));
    cv_.notify_all(); // 通知等待的消费者线程
}

void CamodelHelper::PopMessage() {
    std::unique_ptr<DataHolderBase> data;
    while (isListening_) {
        {
            std::unique_lock<std::mutex> lock(mtx_);
            if (queue_.empty()) {
                cv_.notify_all();
                if (!cv_.wait_for(lock, std::chrono::seconds(1), [this] { return !queue_.empty(); })) {
                    continue;
                }
            }
            data = std::move(queue_.front());
            queue_.pop();
        }
        std::string mes;
        if (data->GetType() == ProfPacketType::ICACHE_LOG) {
            ProfPacketHead head{data->GetType(), static_cast<uint32_t>(sizeof(DvciCacheLog))};
            auto *icacheLog = static_cast<CaLogMessageHolder<DvciCacheLog> *>(data.get());
            mes = Serialize(head, icacheLog->GetData());
        } else if (data->GetType() == ProfPacketType::INSTR_LOG || data->GetType() == ProfPacketType::POPPED_LOG) {
            auto *instrLog = static_cast<CaLogMessageHolder<DvcInstrLog> *>(data.get());
            ProfPacketHead head{data->GetType(), static_cast<uint32_t>(sizeof(DvcInstrLog))};
            mes = Serialize(head, instrLog->GetData());
        } else if (data->GetType() == ProfPacketType::MTE_LOG) {
            auto *mteLog = static_cast<CaLogMessageHolder<DvcMteLog> *>(data.get());
            ProfPacketHead head{data->GetType(), static_cast<uint32_t>(sizeof(DvcMteLog))};
            mes = Serialize(head, mteLog->GetData());
        } else if (data->GetType() == ProfPacketType::CCU_LOG) {
            auto *ccuLog = static_cast<CaLogMessageHolder<DvcCcuLog> *>(data.get());
            ProfPacketHead head{data->GetType(), static_cast<uint32_t>(sizeof(DvcCcuLog))};
            mes = Serialize(head, ccuLog->GetData());
        }
        ProfConfig::Instance().SendMsg(mes);
    }
}
