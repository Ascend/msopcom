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


#include "RemoteProcess.h"
#include <thread>
#include "Communication.h"

RemoteProcess::RemoteProcess(CommType type)
{
    server_ = new Server(type); // NOLINT(cppcoreguidelines-owning-memory)
}

void RemoteProcess::Start() const
{
    if (server_ != nullptr) {
        server_->Start();
    }
}

RemoteProcess::~RemoteProcess()
{
    if (server_ != nullptr) {
        delete server_;
        server_ = nullptr;
    }
}

ssize_t RemoteProcess::Wait(std::size_t clientId, std::string& msg) const
{
    // 如果设置了timeout，那么，等待一段时间后，需要释放
    return server_->Read(clientId, msg);
}

ssize_t RemoteProcess::Notify(std::size_t clientId, const std::string& msg) const
{
    return server_->Write(clientId, msg);
}

void RemoteProcess::SetMsgHandlerHook(ClientMsgHandlerHook &&hook) const
{
    if (server_ != nullptr) {
        server_->SetMsgHandlerHook(std::move(hook));
    }
}
