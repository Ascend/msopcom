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

#ifndef FUNC_INJECTION_CAMODELHELPER_H
#define FUNC_INJECTION_CAMODELHELPER_H
#include <thread>
#include <mutex>
#include <queue>
#include "runtime/inject_helpers/ProfConfig.h"
#include "utils/Future.h"
// 基类（用于类型擦除）
class DataHolderBase {
public:
    virtual ~DataHolderBase() = default;
    virtual ProfPacketType GetType() const = 0;
};

// 模板派生类（存储具体类型）
template <typename T> class CaLogMessageHolder : public DataHolderBase {
public:
    CaLogMessageHolder(T &&value, ProfPacketType t) : type(t), logContent(std::move(value)) {}

    ProfPacketType GetType() const override { return type; }

    const T &GetData() const { return logContent; }

private:
    ProfPacketType type;
    T logContent;
};

class CamodelHelper {
public:
    static CamodelHelper &Instance() {
        static CamodelHelper inst;
        return inst;
    }

    void SendCaLog(std::unique_ptr<DataHolderBase> data);
    void Enable() { enable_ = true; }
    void Disable() { enable_ = false; }
    bool IsEnable() const { return enable_; }

    void SendSync() {
        std::unique_lock<std::mutex> lock(mtx_);
        if (queue_.empty()) {
            return;
        }
        cv_.wait(lock, [this] { return queue_.empty(); });
    }

private:
    CamodelHelper() {
        isListening_ = true;
        sendThread_ = std::thread(&CamodelHelper::PopMessage, this);
    }
    ~CamodelHelper() {
        isListening_ = false;
        if (sendThread_.joinable()) {
            sendThread_.join();
        }
    }
    void PopMessage();
    std::thread sendThread_;
    std::atomic<bool> isListening_{false};
    std::atomic<bool> enable_{false};
    std::mutex mtx_;
    std::queue<std::unique_ptr<DataHolderBase>> queue_;
    std::condition_variable cv_;
};

#endif // FUNC_INJECTION_CAMODELHELPER_H
