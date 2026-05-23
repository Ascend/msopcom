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


#define private public
#include "runtime/inject_helpers/MemGuard.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "runtime/inject_helpers/ConfigManager.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/FuncSelector.h"
#undef private

#include <gtest/gtest.h>
#include "mockcpp/mockcpp.hpp"

aclError MemcpyStub(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind)
{
    std::copy_n(static_cast<const uint8_t*>(src), count, static_cast<uint8_t*>(dst));
    return ACL_ERROR_NONE;
}

aclError MemsetStub(void *devPtr, size_t maxCount, int32_t value, size_t count)
{
    std::fill_n(static_cast<char *>(devPtr), count, static_cast<unsigned char>(value));
    return ACL_ERROR_NONE;
}

void MemGuardTest_MallocRecordAfterCheck(void *actualAddr, void *useraddr, size_t userSize)
{
    size_t frontSize = 0;
    size_t backSize = 0;
    MemoryGuard::Instance().GetGuardSizes(frontSize, backSize);

    // 确认用户地址已修改为实际地址 + 安全区长度
    ASSERT_EQ(reinterpret_cast<char *>(useraddr), reinterpret_cast<char *>(actualAddr) + frontSize);

    GuardBlockInfo blockInfo;
    ASSERT_EQ(MemoryGuard::Instance().GetGuardBlockInfoByUserAddr(useraddr, blockInfo), true);
    ASSERT_EQ(blockInfo.realPtr, actualAddr);
    ASSERT_EQ(blockInfo.userSize, userSize);
    ASSERT_EQ(blockInfo.frontGuardSize, frontSize);
    ASSERT_EQ(blockInfo.backGuardSize, backSize);
}

TEST(MemGuardTestSuit, memGuard_test_set_guard_size)
{
    size_t frontSize = -1;
    size_t backSize = -1;

    // 1、默认值
    MemoryGuard::Instance().GetGuardSizes(frontSize, backSize);
    ASSERT_EQ(frontSize, 0);
    ASSERT_EQ(backSize, 32);

    // 2、0值
    MemoryGuard::Instance().SetGuardSizes(0, 128);
    MemoryGuard::Instance().GetGuardSizes(frontSize, backSize);
    ASSERT_EQ(frontSize, 0);
    ASSERT_EQ(backSize, 128);

    // 3、有效值，向上对齐到 max_align_t 
    MemoryGuard::Instance().SetGuardSizes(10, 70);
    MemoryGuard::Instance().GetGuardSizes(frontSize, backSize);
    ASSERT_EQ(frontSize, 32);
    ASSERT_EQ(backSize, 96);

    // 4、有效值，刚好对齐
    MemoryGuard::Instance().SetGuardSizes(256, 512);
    MemoryGuard::Instance().GetGuardSizes(frontSize, backSize);
    ASSERT_EQ(frontSize, 256);
    ASSERT_EQ(backSize, 512);

    // 5、恢复默认值
    MemoryGuard::Instance().SetGuardSizes(0, 32);
    MemoryGuard::Instance().GetGuardSizes(frontSize, backSize);
    ASSERT_EQ(frontSize, 0);
    ASSERT_EQ(backSize, 32);
}

TEST(MemGuardTestSuit, memGuard_test_set_guard_pattern)
{
    unsigned char pattern = 0x0;

    // 1、默认值
    MemoryGuard::Instance().GetGuardPattern(pattern);
    ASSERT_EQ(pattern, GM_BUFFER_GUARD_DFT_PATTERN);

    // 2、其它值
    MemoryGuard::Instance().SetGuardPattern(0xFF);
    MemoryGuard::Instance().GetGuardPattern(pattern);
    ASSERT_EQ(pattern, 0xFF);
}

TEST(MemGuardTestSuit, memGuard_test_fill_and_check_guard)
{
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(invoke(MemcpyStub));
    MOCKER(&aclrtMemsetImplOrigin).stubs().will(invoke(MemsetStub));

    MemoryGuard::Instance().SetGuardSizes(GM_BUFFER_GUARD_DFT_SIZE, GM_BUFFER_GUARD_DFT_SIZE);
    MemoryGuard::Instance().SetGuardPattern(GM_BUFFER_GUARD_DFT_PATTERN);

    uint32_t bufferSize = 1024;
    uint8_t buffer[bufferSize];

    uint64_t errBytesStart = -1;
    size_t errBytes = 0;

    // 1、填充后检查通过
    MemoryGuard::Instance().FillGuard(buffer, bufferSize);
    MemoryGuard::Instance().CheckGuard(buffer, bufferSize, errBytesStart, errBytes);
    ASSERT_EQ(errBytesStart, -1);
    ASSERT_EQ(errBytes, 0);

    // 2、填充后修改某一字节检查不通过，返回异常字节数
    buffer[50] = 0;
    MemoryGuard::Instance().CheckGuard(buffer, bufferSize, errBytesStart, errBytes);
    ASSERT_EQ(errBytesStart, 50);
    ASSERT_EQ(errBytes, 1);

    // 3、填充后修改多个字节检查不通过，返回异常字节数
    uint32_t start = 50;
    uint32_t bytes = 10;
    for (uint32_t i = start; i < start + bytes; ++i) {
        buffer[i] = 0;
    }

    MemoryGuard::Instance().CheckGuard(buffer, bufferSize, errBytesStart, errBytes);
    ASSERT_EQ(errBytesStart, start);
    ASSERT_EQ(errBytes, 10);
}

TEST(MemGuardTestSuit, memGuard_test_malloc_and_free_proc_normal)
{
    SanitizerConfig config;
    config.memCheck = true;
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(config));

    MemoryGuard::Instance().Init();
    MemoryGuard::Instance().SetGuardSizes(GM_BUFFER_GUARD_DFT_SIZE, GM_BUFFER_GUARD_DFT_SIZE);
    MemoryGuard::Instance().SetGuardPattern(GM_BUFFER_GUARD_DFT_PATTERN);
    
    size_t userSize = 128;
    size_t actualSize = MemoryGuard::Instance().GetTotalSize(userSize);
    ASSERT_EQ(actualSize, userSize + GM_BUFFER_GUARD_DFT_SIZE * 2);  // 检查实际长度
    uint8_t actualAddr[actualSize]; // 模仿malloc的结果
    uint8_t *actualPtr = actualAddr;
    uint8_t *useraddr = actualPtr;

    // 正常添加malloc
    MemoryGuard::Instance().MallocProc(reinterpret_cast<void **>(&useraddr), userSize);
    // 检查表记录存在且符合预期
    ASSERT_EQ(MemoryGuard::Instance().GetGuardMapSize(), 1);
    MemGuardTest_MallocRecordAfterCheck(actualAddr, useraddr, userSize);

    // 模拟kernellaunch时填充安全区
    MemoryGuard::Instance().FillAllMemGuard();
    // 填充后检查
    GuardBlockInfo blockInfoRst{};
    MemoryGuard::Instance().CheckAllMemGuard();
    ASSERT_EQ(MemoryGuard::Instance().GetGuardBlockInfoByUserAddr(useraddr, blockInfoRst), true);
    ASSERT_EQ(blockInfoRst.realPtr, actualPtr);
    ASSERT_EQ(blockInfoRst.userSize, userSize);
    ASSERT_EQ(blockInfoRst.frontGuardSize, GM_BUFFER_GUARD_DFT_SIZE);
    ASSERT_EQ(blockInfoRst.backGuardSize, GM_BUFFER_GUARD_DFT_SIZE);
    ASSERT_EQ(blockInfoRst.frontErrStart, -1);
    ASSERT_EQ(blockInfoRst.backErrStart, -1);
    ASSERT_EQ(blockInfoRst.frontErrBytes, 0);
    ASSERT_EQ(blockInfoRst.backErrBytes, 0);

    // 模拟kernellaunch结束后检查安全区
    MemoryGuard::Instance().CheckAllMemGuard();
    // 获取对应blockinfo，并确认无越界
    ASSERT_EQ(MemoryGuard::Instance().GetGuardBlockInfoByUserAddr(useraddr, blockInfoRst), true);
    ASSERT_EQ(blockInfoRst.realPtr, actualPtr);
    ASSERT_EQ(blockInfoRst.userSize, userSize);
    ASSERT_EQ(blockInfoRst.frontGuardSize, GM_BUFFER_GUARD_DFT_SIZE);
    ASSERT_EQ(blockInfoRst.backGuardSize, GM_BUFFER_GUARD_DFT_SIZE);
    ASSERT_EQ(blockInfoRst.frontErrStart, -1);
    ASSERT_EQ(blockInfoRst.backErrStart, -1);
    ASSERT_EQ(blockInfoRst.frontErrBytes, 0);
    ASSERT_EQ(blockInfoRst.backErrBytes, 0);

    // free
    actualPtr = static_cast<uint8_t *>(MemoryGuard::Instance().GetRealPtr(useraddr));
    ASSERT_EQ(actualPtr, (static_cast<uint8_t *>(actualAddr)));
    MemoryGuard::Instance().FreeProc(useraddr);
    ASSERT_EQ(MemoryGuard::Instance().GetGuardMapSize(), 0);

    // 清空map防止影响后续用例
    MemoryGuard::Instance().ClearGuardBlockMap();
}

TEST(MemGuardTestSuit, memGuard_test_malloc_and_free_proc_except)
{
    SanitizerConfig config;
    config.memCheck = true;
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(config));

    MemoryGuard::Instance().Init();
    MemoryGuard::Instance().SetGuardSizes(GM_BUFFER_GUARD_DFT_SIZE, GM_BUFFER_GUARD_DFT_SIZE);
    MemoryGuard::Instance().SetGuardPattern(GM_BUFFER_GUARD_DFT_PATTERN);
    MOCKER(&MemoryGuard::GenGMAddrErr).stubs();

    size_t userSize = 128;
    size_t actualSize = MemoryGuard::Instance().GetTotalSize(userSize);
    ASSERT_EQ(actualSize, userSize + GM_BUFFER_GUARD_DFT_SIZE * 2);  // 检查实际长度
    uint8_t actualAddr[actualSize]; // 模仿malloc的结果
    uint8_t *actualPtr = actualAddr;
    uint8_t *useraddr = actualAddr;

    // 正常添加malloc
    MemoryGuard::Instance().MallocProc(reinterpret_cast<void **>(&useraddr), userSize);
    // 检查表记录存在且符合预期
    ASSERT_EQ(MemoryGuard::Instance().GetGuardMapSize(), 1);
    MemGuardTest_MallocRecordAfterCheck(actualAddr, useraddr, userSize);

    // 模拟kernellaunch时填充安全区
    MemoryGuard::Instance().FillAllMemGuard();
    // 填充后检查
    GuardBlockInfo blockInfoRst{};
    MemoryGuard::Instance().CheckAllMemGuard();
    ASSERT_EQ(MemoryGuard::Instance().GetGuardBlockInfoByUserAddr(useraddr, blockInfoRst), true);
    ASSERT_EQ(blockInfoRst.realPtr, actualPtr);
    ASSERT_EQ(blockInfoRst.userSize, userSize);
    ASSERT_EQ(blockInfoRst.frontGuardSize, GM_BUFFER_GUARD_DFT_SIZE);
    ASSERT_EQ(blockInfoRst.backGuardSize, GM_BUFFER_GUARD_DFT_SIZE);
    ASSERT_EQ(blockInfoRst.frontErrStart, -1);
    ASSERT_EQ(blockInfoRst.backErrStart, -1);
    ASSERT_EQ(blockInfoRst.frontErrBytes, 0);
    ASSERT_EQ(blockInfoRst.backErrBytes, 0);

    // 模拟越界写
    actualAddr[0] = 0xFF;

    // 模拟kernellaunch结束后检查安全区
    MemoryGuard::Instance().CheckAllMemGuard();
    // 获取对应blockinfo，前安全区有1字节异常
    ASSERT_EQ(MemoryGuard::Instance().GetGuardBlockInfoByUserAddr(useraddr, blockInfoRst), true);
    ASSERT_EQ(blockInfoRst.realPtr, actualPtr);
    ASSERT_EQ(blockInfoRst.userSize, userSize);
    ASSERT_EQ(blockInfoRst.frontGuardSize, GM_BUFFER_GUARD_DFT_SIZE);
    ASSERT_EQ(blockInfoRst.backGuardSize, GM_BUFFER_GUARD_DFT_SIZE);
    ASSERT_EQ(blockInfoRst.frontErrStart, 0);
    ASSERT_EQ(blockInfoRst.backErrStart, -1);
    ASSERT_EQ(blockInfoRst.frontErrBytes, 1);
    ASSERT_EQ(blockInfoRst.backErrBytes, 0);

    // free
    actualPtr = static_cast<uint8_t *>(MemoryGuard::Instance().GetRealPtr(useraddr));
    ASSERT_EQ(actualPtr, (static_cast<uint8_t *>(actualAddr)));
    MemoryGuard::Instance().FreeProc(useraddr);
    ASSERT_EQ(MemoryGuard::Instance().GetGuardMapSize(), 0);

    // 清空map防止影响后续用例
    MemoryGuard::Instance().ClearGuardBlockMap();
}

// 前保护区长度为0，测试仅存在后保护区的场景
TEST(MemGuardTestSuit, memGuard_test_malloc_and_free_proc_except_only_back_guard)
{
    SanitizerConfig config;
    config.memCheck = true;
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(&SanitizerConfigManager::GetConfig).stubs().will(returnValue(config));

    MemoryGuard::Instance().Init();
    // 前保护区长度设置为0
    size_t frontSize = 0;
    size_t backSize = GM_BUFFER_GUARD_DFT_SIZE;
    MemoryGuard::Instance().SetGuardSizes(frontSize, backSize);
    MemoryGuard::Instance().SetGuardPattern(GM_BUFFER_GUARD_DFT_PATTERN);
    MOCKER(&MemoryGuard::GenGMAddrErr).stubs();

    size_t userSize = 128;
    size_t actualSize = MemoryGuard::Instance().GetTotalSize(userSize);

    MemoryGuard::Instance().GetGuardSizes(frontSize, backSize);
    ASSERT_EQ(actualSize, userSize + frontSize + backSize);  // 检查实际长度
    uint8_t actualAddr[actualSize]; // 模仿malloc的结果
    uint8_t *actualPtr = actualAddr;
    uint8_t *useraddr = actualAddr;

    // 正常添加malloc
    MemoryGuard::Instance().MallocProc(reinterpret_cast<void **>(&useraddr), userSize);
    // 检查表记录存在且符合预期
    ASSERT_EQ(MemoryGuard::Instance().GetGuardMapSize(), 1);
    MemGuardTest_MallocRecordAfterCheck(actualAddr, useraddr, userSize);

    // 模拟kernellaunch时填充安全区
    MemoryGuard::Instance().FillAllMemGuard();
    // 填充后检查
    GuardBlockInfo blockInfoRst{};
    MemoryGuard::Instance().CheckAllMemGuard();
    ASSERT_EQ(MemoryGuard::Instance().GetGuardBlockInfoByUserAddr(useraddr, blockInfoRst), true);
    ASSERT_EQ(blockInfoRst.realPtr, actualPtr);
    ASSERT_EQ(blockInfoRst.userSize, userSize);
    ASSERT_EQ(blockInfoRst.frontGuardSize, frontSize);
    ASSERT_EQ(blockInfoRst.backGuardSize, backSize);
    ASSERT_EQ(blockInfoRst.frontErrStart, -1);
    ASSERT_EQ(blockInfoRst.backErrStart, -1);
    ASSERT_EQ(blockInfoRst.frontErrBytes, 0);
    ASSERT_EQ(blockInfoRst.backErrBytes, 0);

    // 模拟越界写
    actualAddr[userSize + 10] = 0xFF;

    // 模拟kernellaunch结束后检查安全区
    MemoryGuard::Instance().CheckAllMemGuard();
    // 获取对应blockinfo，后安全区有1字节异常
    ASSERT_EQ(MemoryGuard::Instance().GetGuardBlockInfoByUserAddr(useraddr, blockInfoRst), true);
    ASSERT_EQ(blockInfoRst.realPtr, actualPtr);
    ASSERT_EQ(blockInfoRst.userSize, userSize);
    ASSERT_EQ(blockInfoRst.frontGuardSize, frontSize);
    ASSERT_EQ(blockInfoRst.backGuardSize, backSize);
    ASSERT_EQ(blockInfoRst.frontErrStart, -1);
    ASSERT_EQ(blockInfoRst.backErrStart, 10);
    ASSERT_EQ(blockInfoRst.frontErrBytes, 0);
    ASSERT_EQ(blockInfoRst.backErrBytes, 1);

    // free
    actualPtr = static_cast<uint8_t *>(MemoryGuard::Instance().GetRealPtr(useraddr));
    ASSERT_EQ(actualPtr, (static_cast<uint8_t *>(actualAddr)));
    MemoryGuard::Instance().FreeProc(useraddr);
    ASSERT_EQ(MemoryGuard::Instance().GetGuardMapSize(), 0);

    // 清空map防止影响后续用例
    MemoryGuard::Instance().ClearGuardBlockMap();
}