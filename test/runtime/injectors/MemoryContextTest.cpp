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

#include <chrono>
#include <sstream>
#include <unistd.h>
#include <sys/syscall.h>

#define private public
#include "utils/InjectLogger.h"
#include "runtime/inject_helpers/MemoryContext.h"
#undef private

#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>

#include <gtest/gtest.h>

#include "runtime/RuntimeOrigin.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "utils/FileSystem.h"
#include "utils/PipeCall.h"
#include "mockcpp/mockcpp.hpp"

constexpr uint64_t MEM_ADDR = 0x12c045400000U;
constexpr uint64_t MEM_SIZE = 0x1000U;

TEST(MemoryContext, record_and_discard_multiple_normal_memory_section)
{
    auto &inst = MemoryContext::Instance();
    MemoryContext::Instance().DiscardAll();
    uint32_t count = 0U;
    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR), MEM_SIZE);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR + MEM_SIZE), 2 * MEM_SIZE);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    MemoryContext::Instance().Discard(reinterpret_cast<void *>(MEM_ADDR));
    count--;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    MemoryContext::Instance().Discard(reinterpret_cast<void *>(MEM_ADDR + MEM_SIZE));
    count--;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);
}

TEST(MemoryContext, record_null_memory_section)
{
    auto &inst = MemoryContext::Instance();
    MemoryContext::Instance().DiscardAll();
    uint32_t count = 0U;
    MemoryContext::Instance().Append(nullptr, MEM_SIZE);
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);
}

TEST(MemoryContext, record_and_discard_the_same_memory_section)
{
    auto &inst = MemoryContext::Instance();
    uint32_t count = 0U;
    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR), MEM_SIZE);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);
    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR), MEM_SIZE * 2);
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    MemoryContext::Instance().Discard(reinterpret_cast<void *>(MEM_ADDR));
    count--;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR), MEM_SIZE);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    MemoryContext::Instance().DiscardAll();
    count = 0U;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);
}

TEST(MemoryContext, record_and_discard_all_memory_section)
{
    auto &inst = MemoryContext::Instance();
    uint32_t count = 0U;

    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR), MEM_SIZE);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR + MEM_SIZE), MEM_SIZE * 2);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    MemoryContext::Instance().DiscardAll();
    count = 0U;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);
}

TEST(MemoryContext, create_snapshot_for_memory_without_stream_and_restore_success)
{
    GlobalMockObject::verify();
    auto &inst = MemoryContext::Instance();
    uint32_t count = 0U;

    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR), MEM_SIZE);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR + MEM_SIZE), MEM_SIZE * 2);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    bool ret = false;
    void *addr = reinterpret_cast<void *>(MEM_ADDR);
    MOCKER(&aclrtMallocImplOrigin)
        .expects(exactly(2))
        .with(outBoundP(&addr, sizeof(void *)), any(), any())
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtMemcpyImplOrigin)
        .expects(exactly(4))
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtFreeImplOrigin)
        .expects(exactly(2))
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtCtxGetCurrentDefaultStreamImplOrigin)
        .stubs()
        .will(returnValue(ACL_ERROR_BAD_ALLOC));
    ret = inst.Backup();
    ASSERT_EQ(ret, true);

    ret = inst.Restore();
    ASSERT_EQ(ret, true);

    MemoryContext::Instance().DiscardAll();
    count = 0U;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);
    GlobalMockObject::verify();
}

TEST(MemoryContext, create_snapshot_for_memory_with_stream_and_restore_success)
{
    GlobalMockObject::verify();
    auto &inst = MemoryContext::Instance();
    uint32_t count = 0U;

    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR), MEM_SIZE);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR + MEM_SIZE), MEM_SIZE * 2);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    bool ret = false;
    void *addr = reinterpret_cast<void *>(MEM_ADDR);
    aclrtStream stream = reinterpret_cast<void *>(0xfffff8c0U);

    MOCKER(&aclrtMallocImplOrigin)
        .expects(exactly(2))
        .with(outBoundP(&addr, sizeof(void *)), any(), any())
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtMemcpyAsyncImplOrigin)
        .defaults()
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtSynchronizeStreamImplOrigin)
        .defaults()
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtCtxGetCurrentDefaultStreamImplOrigin)
        .expects(once())
        .with(outBoundP(&stream, sizeof(stream)))
        .will(returnValue(ACL_SUCCESS));
    ret = inst.Backup();
    ASSERT_EQ(ret, true);

    ret = inst.Restore();
    ASSERT_EQ(ret, true);

    MemoryContext::Instance().DiscardAll();
    count = 0U;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);
    GlobalMockObject::verify();
}

TEST(MemoryContext, create_snapshot_failed_for_invalid_addr)
{
    auto &inst = MemoryContext::Instance();
    uint32_t count = 0U;
    MemoryContext::Instance().DiscardAll();
    int a = 10;
    MemoryContext::Instance().Append(&a, MEM_SIZE);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    bool ret = false;
    ret = inst.Backup();
    ASSERT_EQ(ret, false);

    MemoryContext::Instance().DiscardAll();
    count = 0U;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);
}

TEST(MemoryContext, create_snapshot_failed_for_invalid_section)
{
    auto &inst = MemoryContext::Instance();
    MemoryContext::Instance().DiscardAll();
    uint32_t count = 0U;
    MemoryContext::MemSection section;
    section.originAddr = nullptr;
    ASSERT_EQ(MemoryContext::Instance().CreateSnapshot(section), false);
}

TEST(MemoryContext, create_snapshot_failed_due_to_alloc_failure)
{
    GlobalMockObject::verify();
    auto &inst = MemoryContext::Instance();
    uint32_t count = 0U;

    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR), MEM_SIZE);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    bool ret = false;
    MOCKER(aclrtMallocImplOrigin)
        .expects(once())
        .will(returnValue(ACL_ERROR_BAD_ALLOC));
    ret = inst.Backup();
    ASSERT_EQ(ret, false);

    MemoryContext::Instance().DiscardAll();
    count = 0U;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);
    GlobalMockObject::verify();
}

TEST(MemoryContext, create_snapshot_without_stream_failed_due_to_memcpy_failure)
{
    GlobalMockObject::verify();
    auto &inst = MemoryContext::Instance();
    uint32_t count = 0U;

    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR), MEM_SIZE);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    bool ret = false;
    void *addr = reinterpret_cast<void *>(MEM_ADDR);
    MOCKER(&aclrtMallocImplOrigin)
        .expects(once())
        .with(outBoundP(&addr, sizeof(void *)), any(), any())
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtMemcpyImplOrigin)
        .expects(once())
        .will(returnValue(ACL_ERROR_BAD_ALLOC));
    MOCKER(&aclrtCtxGetCurrentDefaultStreamImplOrigin)
        .defaults()
        .will(returnValue(ACL_ERROR_BAD_ALLOC));
    ret = inst.Backup();
    ASSERT_EQ(ret, false);

    MemoryContext::Instance().DiscardAll();
    count = 0U;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);
    GlobalMockObject::verify();
}

TEST(MemoryContext, create_snapshot_with_stream_failed_due_to_memcpy_failure)
{
    GlobalMockObject::verify();
    auto &inst = MemoryContext::Instance();
    uint32_t count = 0U;

    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR), MEM_SIZE);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    bool ret = false;
    void *addr = reinterpret_cast<void *>(MEM_ADDR);
    rtStream_t stream = reinterpret_cast<void *>(0xfffff8c0U);
    MOCKER(&aclrtMallocImplOrigin)
        .expects(once())
        .with(outBoundP(&addr, sizeof(void *)), any(), any())
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtMemcpyAsyncImplOrigin)
        .expects(once())
        .will(returnValue(ACL_ERROR_BAD_ALLOC));
    MOCKER(&aclrtCtxGetCurrentDefaultStreamImplOrigin)
        .defaults()
        .with(outBoundP(&stream, sizeof(stream)))
        .will(returnValue(ACL_SUCCESS));
    ret = inst.Backup();
    ASSERT_EQ(ret, false);

    MemoryContext::Instance().DiscardAll();
    count = 0U;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);
    GlobalMockObject::verify();
}

TEST(MemoryContext, memcpy_with_stream_success)
{
    GlobalMockObject::verify();
    auto &inst = MemoryContext::Instance();
    rtStream_t stream = reinterpret_cast<void *>(0xfffff8c0U);

    MemoryContext::MemSection section = {reinterpret_cast<void *>(MEM_ADDR),
        reinterpret_cast<void *>(MEM_ADDR + MEM_SIZE), MEM_SIZE, aclrtMemMallocPolicy::ACL_MEM_MALLOC_HUGE_FIRST};
    bool ret = false;
    MOCKER(&aclrtMemcpyAsyncImplOrigin)
        .defaults()
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtSynchronizeStreamImplOrigin)
        .defaults()
        .will(returnValue(ACL_SUCCESS));
    ret = inst.MemCopyWithStream(section, stream,
        MemoryContext::MemCopyDirection::ORIGIN_TO_SNAPSHOT);
    ASSERT_EQ(ret, true);

    ret = inst.MemCopyWithStream(section, stream,
        MemoryContext::MemCopyDirection::SNAPSHOT_TO_ORIGIN);
    ASSERT_EQ(ret, true);

    MemoryContext::Instance().DiscardAll();
    GlobalMockObject::verify();
}

TEST(MemoryContext, memcpy_with_stream_failed_due_to_stream_sync_failure)
{
    GlobalMockObject::verify();
    auto &inst = MemoryContext::Instance();
    rtStream_t stream = reinterpret_cast<void *>(0xfffff8c0U);

    MemoryContext::MemSection section = {reinterpret_cast<void *>(MEM_ADDR),
        reinterpret_cast<void *>(MEM_ADDR + MEM_SIZE), MEM_SIZE};
    bool ret = false;
    MOCKER(&aclrtMemcpyAsyncImplOrigin)
        .defaults()
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtSynchronizeStreamImplOrigin)
        .defaults()
        .will(returnValue(ACL_ERROR_BAD_ALLOC));
    ret = inst.MemCopyWithStream(section, stream,
        MemoryContext::MemCopyDirection::ORIGIN_TO_SNAPSHOT);
    ASSERT_EQ(ret, false);

    ret = inst.MemCopyWithStream(section, stream,
        MemoryContext::MemCopyDirection::SNAPSHOT_TO_ORIGIN);
    ASSERT_EQ(ret, false);

    MemoryContext::Instance().DiscardAll();
    GlobalMockObject::verify();
}

TEST(MemoryContext, memcpy_with_stream_failed_due_to_memcpyasync_failure)
{
    GlobalMockObject::verify();
    auto &inst = MemoryContext::Instance();
    rtStream_t stream = reinterpret_cast<void *>(0xfffff8c0U);

    MemoryContext::MemSection section = {reinterpret_cast<void *>(MEM_ADDR),
        reinterpret_cast<void *>(MEM_ADDR + MEM_SIZE), MEM_SIZE};
    bool ret = false;
    MOCKER(&aclrtMemcpyAsyncImplOrigin)
        .defaults()
        .will(returnValue(ACL_ERROR_BAD_ALLOC));
    ret = inst.MemCopyWithStream(section, stream,
        MemoryContext::MemCopyDirection::ORIGIN_TO_SNAPSHOT);
    ASSERT_EQ(ret, false);

    ret = inst.MemCopyWithStream(section, stream,
        MemoryContext::MemCopyDirection::SNAPSHOT_TO_ORIGIN);
    ASSERT_EQ(ret, false);

    MemoryContext::Instance().DiscardAll();
    GlobalMockObject::verify();
}

TEST(MemoryContext, memcpy_sync_success)
{
    GlobalMockObject::verify();
    auto &inst = MemoryContext::Instance();

    MemoryContext::MemSection section = {reinterpret_cast<void *>(MEM_ADDR),
        reinterpret_cast<void *>(MEM_ADDR + MEM_SIZE), MEM_SIZE};
    bool ret = false;
    MOCKER(&aclrtMemcpyImplOrigin)
        .defaults()
        .will(returnValue(ACL_SUCCESS));
    ret = inst.MemCopySync(section,
        MemoryContext::MemCopyDirection::ORIGIN_TO_SNAPSHOT);
    ASSERT_EQ(ret, true);

    ret = inst.MemCopySync(section,
        MemoryContext::MemCopyDirection::SNAPSHOT_TO_ORIGIN);
    ASSERT_EQ(ret, true);

    MemoryContext::Instance().DiscardAll();
    GlobalMockObject::verify();
}

TEST(MemoryContext, memcpy_sync_failed_due_to_memcpy_failure)
{
    GlobalMockObject::verify();
    auto &inst = MemoryContext::Instance();

    MemoryContext::MemSection section = {reinterpret_cast<void *>(MEM_ADDR),
        reinterpret_cast<void *>(MEM_ADDR + MEM_SIZE), MEM_SIZE};
    bool ret = false;
    MOCKER(&aclrtMemcpyImplOrigin)
        .defaults()
        .will(returnValue(ACL_ERROR_BAD_ALLOC));
    ret = inst.MemCopySync(section,
        MemoryContext::MemCopyDirection::ORIGIN_TO_SNAPSHOT);
    ASSERT_EQ(ret, false);

    ret = inst.MemCopySync(section,
        MemoryContext::MemCopyDirection::SNAPSHOT_TO_ORIGIN);
    ASSERT_EQ(ret, false);

    MemoryContext::Instance().DiscardAll();
    GlobalMockObject::verify();
}

TEST(MemoryContext, restore_failed_due_to_memcpy_failure)
{
    auto &inst = MemoryContext::Instance();
    uint32_t count = 0U;

    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR), MEM_SIZE);
    count++;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);

    bool ret = false;
    void *addr = reinterpret_cast<void *>(MEM_ADDR);
    rtStream_t stream = reinterpret_cast<void *>(0xfffff8c0U);
    MOCKER(&aclrtMallocImplOrigin)
        .defaults()
        .with(outBoundP(&addr, sizeof(void *)), any(), any())
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtMemcpyAsyncImplOrigin)
        .defaults()
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtFreeImplOrigin)
        .defaults()
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtCtxGetCurrentDefaultStreamImplOrigin)
        .defaults()
        .with(outBoundP(&stream, sizeof(stream)))
        .will(returnValue(ACL_SUCCESS));
    MOCKER(&aclrtSynchronizeStreamImplOrigin)
        .defaults()
        .will(returnValue(ACL_SUCCESS));

    ret = inst.Backup();
    ASSERT_EQ(ret, true);

    MOCKER(&aclrtMemcpyAsyncImplOrigin)
        .expects(once())
        .will(returnValue(ACL_ERROR_BAD_ALLOC));
    auto &logger = InjectLogger::Instance();
    auto origLv = logger.GetLogLv();
    logger.lv_ = LogLv::DEBUG;
    testing::internal::CaptureStdout();
    ret = inst.Restore();
    std::string output = testing::internal::GetCapturedStdout();
    logger.lv_ = origLv;
    ASSERT_EQ(ret, true);
    ASSERT_TRUE(output.find("[DEBUG]") != std::string::npos);
    ASSERT_TRUE(output.find("continue restoring remaining regions") != std::string::npos);

    MemoryContext::Instance().DiscardAll();
    count = 0U;
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), count);
    GlobalMockObject::verify();
}

TEST(MemoryContext, restore_failed_due_to_invalid_addr)
{
    auto &inst = MemoryContext::Instance();
    MemoryContext::MemSection section = {nullptr,
        reinterpret_cast<void *>(MEM_ADDR + MEM_SIZE), MEM_SIZE};
    bool ret = false;

    auto &logger = InjectLogger::Instance();
    auto origLv = logger.GetLogLv();
    logger.lv_ = LogLv::DEBUG;

    // originAddr == nullptr → ERROR_LOG, return false
    testing::internal::CaptureStdout();
    ret = inst.RestoreFromSnapshot(section);
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_EQ(ret, false);
    ASSERT_TRUE(output.find("[ERROR]") != std::string::npos);
    ASSERT_TRUE(output.find("failed to restore an invalid addr") != std::string::npos);

    // snapshotAddr == nullptr → DEBUG_LOG, return false
    section.originAddr = reinterpret_cast<void *>(MEM_ADDR);
    section.snapshotAddr = nullptr;
    testing::internal::CaptureStdout();
    ret = inst.RestoreFromSnapshot(section);
    output = testing::internal::GetCapturedStdout();
    ASSERT_EQ(ret, false);
    ASSERT_TRUE(output.find("[DEBUG]") != std::string::npos);
    ASSERT_TRUE(output.find("there is no snapshot memory allocated") != std::string::npos);

    logger.lv_ = origLv;
}

TEST(MemoryContext, restore_skip_unbackup_memory_and_continue) {
    GlobalMockObject::verify();
    auto &inst = MemoryContext::Instance();
    MemoryContext::Instance().DiscardAll();

    // Append memory but do not call Backup, so snapshotAddr remains nullptr
    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR), MEM_SIZE);
    MemoryContext::Instance().Append(reinterpret_cast<void *>(MEM_ADDR + MEM_SIZE), MEM_SIZE * 2);
    ASSERT_EQ(inst.GetBucket().memSectionMap.size(), 2U);

    auto &logger = InjectLogger::Instance();
    auto origLv = logger.GetLogLv();
    logger.lv_ = LogLv::DEBUG;

    testing::internal::CaptureStdout();
    bool ret = inst.Restore();
    std::string output = testing::internal::GetCapturedStdout();
    logger.lv_ = origLv;

    // Restore should not be interrupted and return true
    ASSERT_EQ(ret, true);
    // Should have printed DEBUG log for unbacked memory regions
    ASSERT_TRUE(output.find("[DEBUG]") != std::string::npos);
    ASSERT_TRUE(output.find("there is no snapshot memory allocated") != std::string::npos);
    ASSERT_TRUE(output.find("continue restoring remaining regions") != std::string::npos);

    MemoryContext::Instance().DiscardAll();
    GlobalMockObject::verify();
}
