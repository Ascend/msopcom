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

#include "utils/Protocol.h"
#include <gtest/gtest.h>
#define private public
#define protected public
#include "acl_rt_impl/HijackedFunc.h"
#undef private
#undef protected
#include "core/DomainSocket.h"
#include "core/FuncSelector.h"
#include "core/LocalProcess.h"
#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/MemGuard.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "utils/Serialize.h"

namespace {
size_t allocatedSize = 0;
std::string mallocRecord;
aclError RecordAlignedMalloc(void **devPtr, size_t size, aclrtMemMallocPolicy) {
    allocatedSize = size;
    *devPtr = reinterpret_cast<void *>(0x1000);
    return ACL_SUCCESS;
}
int CaptureMallocRecord(LocalProcess *, std::string message) {
    mallocRecord = message;
    return message.size();
}
}
TEST(HijackedFuncOfAclrtMallocAlign32Impl, call_passes_guarded_size_to_allocator) {
    allocatedSize = 0;
    MOCKER(&MemoryGuard::GetTotalSize).stubs().will(returnValue(size_t{96}));
    MOCKER(IsSanitizer).stubs().will(returnValue(false));
    MOCKER(IsOpProf).stubs().will(returnValue(false));

    HijackedFuncOfAclrtMallocAlign32Impl instance;
    instance.originfunc_ = RecordAlignedMalloc;
    void *devPtr = nullptr;
    EXPECT_EQ(instance.Call(&devPtr, 33, ACL_MEM_MALLOC_HUGE_FIRST), ACL_SUCCESS);
    EXPECT_EQ(allocatedSize, 96U);
    EXPECT_EQ(devPtr, reinterpret_cast<void *>(0x1000));
    EXPECT_EQ(instance.size_, 33U);
    GlobalMockObject::verify();
}
TEST(HijackedFuncOfAclrtMallocAlign32Impl, sanitizer_reports_aligned_user_size_after_success) {
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(aclrtGetDeviceImplOrigin).stubs().will(returnValue(ACL_SUCCESS));
    MOCKER(&LocalDevice::Notify).stubs().will(invoke(CaptureMallocRecord));
    mallocRecord.clear();
    void *devPtr = reinterpret_cast<void *>(0x1000);
    HijackedFuncOfAclrtMallocAlign32Impl instance;
    instance.Pre(&devPtr, 33, ACL_MEM_MALLOC_HUGE_FIRST);
    EXPECT_EQ(instance.Post(ACL_SUCCESS), ACL_SUCCESS);
    PacketHead head{PacketType::MEMORY_RECORD};
    HostMemRecord record{};
    record.type = MemOpType::MALLOC;
    record.infoSrc = MemInfoSrc::ACL;
    record.dstAddr = 0x1000;
    record.memSize = 64;
    EXPECT_EQ(mallocRecord, Serialize(head, record));
    mallocRecord.clear();
    devPtr = nullptr;
    instance.Pre(&devPtr, 33, ACL_MEM_MALLOC_HUGE_FIRST);
    EXPECT_EQ(instance.Post(ACL_ERROR_BAD_ALLOC), ACL_ERROR_BAD_ALLOC);
    EXPECT_TRUE(mallocRecord.empty());
    GlobalMockObject::verify();
}
namespace {
std::string reportedRecord;
int CaptureHostMemoryRecord(LocalProcess *, std::string message) {
    reportedRecord = message;
    return message.size();
}
}
class HostMemoryHooksTest : public testing::Test {
protected:
    void SetUp() override
    {
        MOCKER(IsSanitizer).stubs().will(returnValue(true));
        MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
        MOCKER(&LocalDevice::Notify).stubs().will(invoke(CaptureHostMemoryRecord));
        reportedRecord.clear();
    }
    void TearDown() override { GlobalMockObject::verify(); }
    void ExpectRecord(MemOpType type, uint64_t address, uint64_t size) {
        PacketHead head{PacketType::MEMORY_RECORD};
        HostMemRecord record{};
        record.type = type;
        record.infoSrc = MemInfoSrc::ACL;
        record.dstAddr = address;
        record.memSize = size;
        EXPECT_EQ(reportedRecord, Serialize(head, record));
    }
};

TEST_F(HostMemoryHooksTest, both_host_allocators_report_runtime_compatible_size) {
    void *hostPtr = reinterpret_cast<void *>(0x2000);
    HijackedFuncOfAclrtMallocHostImpl hook;
    hook.Pre(&hostPtr, 33);
    EXPECT_EQ(hook.Post(ACL_SUCCESS), ACL_SUCCESS);
    ExpectRecord(MemOpType::MALLOC, 0x2000, 96);
    reportedRecord.clear();
    hostPtr = reinterpret_cast<void *>(0x3000);
    aclrtMallocConfig cfg{};
    HijackedFuncOfAclrtMallocHostWithCfgImpl configuredHook;
    configuredHook.Pre(&hostPtr, 33, &cfg);
    EXPECT_EQ(configuredHook.Post(ACL_SUCCESS), ACL_SUCCESS);
    ExpectRecord(MemOpType::MALLOC, 0x3000, 96);
    reportedRecord.clear();
    hostPtr = nullptr;
    hook.Pre(&hostPtr, 33);
    EXPECT_EQ(hook.Post(ACL_ERROR_BAD_ALLOC), ACL_ERROR_BAD_ALLOC);
    EXPECT_TRUE(reportedRecord.empty());
    HijackedFuncOfAclrtFreeHostImpl freeHook;
    freeHook.Pre(reinterpret_cast<void *>(0x2000));
    ExpectRecord(MemOpType::FREE, 0x2000, 0);
}
