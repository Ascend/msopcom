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


#include <gtest/gtest.h>
#include <cstring>
#include "mockcpp/mockcpp.hpp"
#define private public
#include "runtime/inject_helpers/KernelReplacement.h"
#undef private

#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/LaunchManager.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "runtime/RuntimeOrigin.h"
#include "utils/FileSystem.h"

using namespace std;

TEST(KernelReplacement, input_empty_handle_then_call_create_expect_false)
{
    EXPECT_FALSE(KernelReplacement::Instance().CreateHandle(nullptr, 0));
}

void PrepareCreateHandle(uint64_t mockRegId)
{
    KernelMatcher::Config config;
    config.launchId = 0;
    string fakekernelPath = "kernel.bin";
    KernelReplacement::Instance().Init(fakekernelPath, config);
    uint64_t(KernelContext::*funcPtr)(uint64_t) = &KernelContext::GetRegisterId;

    MOCKER(funcPtr).stubs().will(returnObjectList(mockRegId, mockRegId, 1));
    KernelContext::RegisterEvent event;
    rtDevBinary_t bin;
    event.bin = bin;
    MOCKER(&KernelContext::GetRegisterEvent).stubs().with(any(), outBound(event)).will(returnValue(true));

    MOCKER(&ReadBinary).stubs().will(returnValue(1));

    const int mockSize = 10;
    vector<uint8_t> handleData(10);
    void *handleAddr = reinterpret_cast<void *>(handleData.data());
    MOCKER(&rtRegisterAllKernelOrigin).stubs()
        .with(any(), outBoundP(&handleAddr, sizeof(void *)))
        .will(returnValue(RT_ERROR_NONE));
}

TEST(KernelReplacement, init_valid_config_then_repeat_call_create_expect_success)
{
    uint64_t mockRegId = 11;
    PrepareCreateHandle(mockRegId);
    const int mockSize = 10;
    uint64_t mockData[mockSize] = {0};
    EXPECT_TRUE(KernelReplacement::Instance().CreateHandle((void **)&mockData, 0));
    EXPECT_EQ(KernelReplacement::Instance().oldKernelRegId_, mockRegId);
    EXPECT_TRUE(KernelReplacement::Instance().replaceTask_);
    EXPECT_TRUE(KernelReplacement::Instance().CreateHandle((void **)&mockData, 0));
    EXPECT_FALSE(KernelReplacement::Instance().CreateHandle((void **)&mockData, 0));

    GlobalMockObject::verify();
}

TEST(KernelReplacement, input_nullptr_then_call_release_expect_false)
{
    ASSERT_FALSE(KernelReplacement::Instance().ReleaseHandle(nullptr));
}

TEST(KernelReplacement, create_handle_success_then_call_release_expect_true)
{
    uint64_t mockRegId = 11;
    PrepareCreateHandle(mockRegId);
    const int mockSize = 10;
    uint64_t mockData[mockSize] = {0};
    uint64_t(KernelContext::*funcPtr)(const void*, bool) = &KernelContext::GetRegisterId;

    MOCKER(funcPtr).stubs().will(returnObjectList(mockRegId, mockRegId, 1));
    EXPECT_TRUE(KernelReplacement::Instance().CreateHandle((void **)&mockData, 0));
    EXPECT_TRUE(KernelReplacement::Instance().ReleaseHandle(mockData));
    GlobalMockObject::verify();
}

TEST(KernelReplaceTask, set_read_binary_failed_then_test_run_expect_fail)
{
    MOCKER(&KernelContext::GetRegisterEvent).stubs().will(returnValue(true));
    MOCKER(&ReadBinary).stubs().will(returnValue(0U));
    const int mockSize = 10;
    uint64_t mockData[mockSize] = {0};
    KernelReplaceTask task("");
    EXPECT_FALSE(task.Run((void **)&mockData, 0, false));
    GlobalMockObject::verify();
}

TEST(KernelReplaceTask, set_rt_register_all_failed_then_test_run_expect_fail)
{
    MOCKER(&KernelContext::GetRegisterEvent).stubs().will(returnValue(true));
    MOCKER(&ReadBinary).stubs().will(returnValue(1U));
    MOCKER(&rtRegisterAllKernelOrigin).stubs()
        .will(returnValue(RT_ERROR_RESERVED));
    const int mockSize = 10;
    uint64_t mockData[mockSize] = {0};
    KernelReplaceTask task("");
    EXPECT_FALSE(task.Run((void **)&mockData, 0, false));
    GlobalMockObject::verify();
}

TEST(KernelDumper, set_match_config_test_dump_expect_success)
{
    MOCKER(&MkdirRecusively).stubs().will(returnValue(true));
    MOCKER(&KernelContext::Save).stubs().will(returnValue(true));
    KernelMatcher::Config config{};
    config.launchId = 0;
    KernelDumper::Instance().Init("./", config);
    EXPECT_TRUE(KernelDumper::Instance().Dump(0));
    KernelDumper::Instance() = KernelDumper();
    GlobalMockObject::verify();
}

namespace {
rtStream_t registeredDumpStream = nullptr;
bool registeredCallbackBlocks = false;
aclrtCallback registeredAclCallback = nullptr;
void *registeredAclCallbackData = nullptr;
rtError_t CaptureDumpCallback(rtCallback_t callback, void *data, rtStream_t stream, bool isBlock) {
    EXPECT_EQ(callback, &KernelDumper::DumpDataCallback);
    EXPECT_NE(data, nullptr);
    registeredDumpStream = stream;
    registeredCallbackBlocks = isBlock;
    return RT_ERROR_NONE;
}
aclError CaptureAclDumpCallback(aclrtCallback callback, void *data, aclrtCallbackBlockType blockType,
    aclrtStream stream) {
    EXPECT_EQ(blockType, ACL_CALLBACK_BLOCK);
    EXPECT_EQ(stream, registeredDumpStream);
    registeredAclCallback = callback;
    registeredAclCallbackData = data;
    return ACL_SUCCESS;
}
}

TEST(KernelDumper, launch_dump_task_registers_runtime_and_acl_callbacks) {
    KernelDumper::Instance() = KernelDumper();
    MOCKER(&KernelContext::DeviceContext::SubscribeReport).stubs().will(returnValue(true));
    MOCKER(rtCallbackLaunchOrigin).stubs().will(invoke(CaptureDumpCallback));
    auto launchCtx = std::make_shared<LaunchContext>(nullptr, nullptr, LaunchParam{1, nullptr, false, 7});
    MOCKER(&LaunchManager::GetLastContext).stubs().will(returnValue(launchCtx));
    MOCKER(aclrtLaunchCallbackImplOrigin).stubs().will(invoke(CaptureAclDumpCallback));
    int streamStorage = 0;
    auto stream = static_cast<rtStream_t>(&streamStorage);
    registeredDumpStream = nullptr;
    registeredCallbackBlocks = false;
    EXPECT_TRUE(KernelDumper::Instance().LaunchDumpTask(stream));
    EXPECT_EQ(registeredDumpStream, stream);
    EXPECT_TRUE(registeredCallbackBlocks);
    registeredAclCallback = nullptr;
    registeredAclCallbackData = nullptr;
    EXPECT_TRUE(KernelDumper::Instance().LaunchDumpTask(stream, true));
    ASSERT_NE(registeredAclCallback, nullptr);
    ASSERT_NE(registeredAclCallbackData, nullptr);
    struct CallbackData { uint64_t launchId; bool aclNew; } callbackData{};
    std::memcpy(&callbackData, registeredAclCallbackData, sizeof(callbackData));
    EXPECT_EQ(callbackData.launchId, 7U);
    EXPECT_TRUE(callbackData.aclNew);
    registeredAclCallback(registeredAclCallbackData);
    KernelDumper::Instance() = KernelDumper();
    GlobalMockObject::verify();
}
