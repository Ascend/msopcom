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

#include "gtest/gtest.h"
#include "mockcpp/mockcpp.hpp"
#define private public
#define protected public
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/HijackedFunc.h"
#undef private
#undef protected

#include "runtime/inject_helpers/KernelContext.h"
#include "core/FuncSelector.h"
#include "utils/InjectLogger.h"
#include "core/DomainSocket.h"
#include "helper/MockHelper.h"

TEST(HijackedFuncOfIpcCloseMemory, normal_calling)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    HijackedFuncOfIpcCloseMemory instance;
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    const void *ptr = reinterpret_cast<void*>(0x12005810000);
    ASSERT_EQ(instance.Call(ptr), RT_ERROR_RESERVED);

    auto func = [](const void *ptr) -> rtError_t {
        return RT_ERROR_NONE;
    };
    instance.originfunc_ = func;
    ASSERT_EQ(instance.Call(ptr), RT_ERROR_NONE);
}


TEST(HijackedFuncOfIpcCloseMemory, origin_bad_calling)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    HijackedFuncOfIpcCloseMemory instance;
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    const void *ptr = reinterpret_cast<void*>(0x12005810000);
    ASSERT_EQ(instance.Call(ptr), RT_ERROR_RESERVED);

    auto func = [](const void *ptr) -> rtError_t {
        return RT_ERROR_INVALID_VALUE;
    };
    instance.originfunc_ = func;
    ASSERT_NE(instance.Call(ptr), RT_ERROR_NONE);
}

TEST(HijackedFuncOfIpcCloseMemory, origin_calling_with_unmatch_response)
{
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(&LocalDevice::Notify).stubs().will(returnValue(0));
    MockHelper::Instance().SetMsg(Serialize(PacketType::IPC_RESPONSE,
        IPCResponse{.type = IPCOperationType::UNMAP_INFO, .status = ResponseStatus::SUCCESS}));

    MOCKER(&LocalDevice::Wait)
        .stubs()
        .will(invoke(MockHelper::WaitMockFuncF));
    HijackedFuncOfIpcCloseMemory instance;
    FuncSelector::Instance().Set(ToolType::SANITIZER);
    const void *ptr = reinterpret_cast<void*>(0x12005810000);
    ASSERT_EQ(instance.Call(ptr), RT_ERROR_RESERVED);

    auto func = [](const void *ptr) -> rtError_t {
        return RT_ERROR_INVALID_VALUE;
    };
    instance.originfunc_ = func;
    ASSERT_NE(instance.Call(ptr), RT_ERROR_NONE);

    MockHelper::Instance().SetMsg(Serialize(PacketType::IPC_RESPONSE,
        IPCResponse{.type = IPCOperationType::MAP_INFO, .status = ResponseStatus::SUCCESS}));
    instance.originfunc_ = func;
    ASSERT_NE(instance.Call(ptr), RT_ERROR_NONE);

    MockHelper::Instance().SetMsg(Serialize(PacketType::IPC_RESPONSE,
        IPCResponse{.type = IPCOperationType::MAP_INFO, .status = ResponseStatus::FAIL}));
    instance.originfunc_ = func;
    ASSERT_NE(instance.Call(ptr), RT_ERROR_NONE);
}

TEST(HijackedFuncOfIpcCloseMemory, test_Wait_mocker)
{
    IPCResponse responseStruct{.type = IPCOperationType::UNMAP_INFO, .status = ResponseStatus::SUCCESS};
    std::string response = Serialize(
        PacketType::IPC_RESPONSE, responseStruct);
    MockHelper::Instance().SetMsg(response);
    MOCKER(&LocalDevice::Wait)
        .stubs()
        .will(invoke(MockHelper::WaitMockFuncF));
    std::string msg;
    ASSERT_EQ(LocalDevice::GetInstance(0, CommType::SOCKET).Wait(msg, 5), response.size());
    PacketType type;
    Deserialize(msg, type);
    ASSERT_EQ(type, PacketType::IPC_RESPONSE);
    IPCResponse deserializedResponseStruct;
    Deserialize(msg.substr(sizeof(type)), deserializedResponseStruct);
    ASSERT_EQ(responseStruct.type, deserializedResponseStruct.type);
    ASSERT_EQ(responseStruct.status, deserializedResponseStruct.status);
}
