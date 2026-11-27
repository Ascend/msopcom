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

#include "mockcpp/mockcpp.hpp"
#include "acl_rt_impl/AscendclImplOrigin.h"
#define private public
#include "runtime/inject_helpers/DevMemManager.h"
#undef private

namespace {

aclError MallocOriginStub(void **devPtr, size_t size, aclrtMemMallocPolicy policy)
{
    if (devPtr == nullptr) {
        return ACL_ERROR_BAD_ALLOC;
    }
    *devPtr = malloc(size);
    return ACL_SUCCESS;
}

aclError FreeOriginStub(void *devPtr)
{
    free(devPtr);
    return ACL_SUCCESS;
}

aclError GetDeviceOriginStub(int32_t *devId)
{
    *devId = 0;
    return ACL_SUCCESS;
}

} // namespace [Dummy]

TEST(DevMemManager, set_memory_init_flag_expect_get_correct_init_flag)
{
    DevMemManager &devMemManager = DevMemManager::Instance();
    devMemManager.SetMemoryInitFlag(true);
    ASSERT_TRUE(devMemManager.IsMemoryInit());
    devMemManager.SetMemoryInitFlag(false);
    ASSERT_FALSE(devMemManager.IsMemoryInit());
}

TEST(DevMemManager, malloc_with_nullptr_expect_return_error)
{
    DevMemManager &devMemManager = DevMemManager::Instance();
    ASSERT_NE(devMemManager.MallocMemory(nullptr, 0), ACL_SUCCESS);
}

TEST(DevMemManager, malloc_with_valid_pointer_expect_return_valid_memory)
{
    MOCKER(&aclrtMallocImplOrigin).stubs().will(invoke(MallocOriginStub));
    MOCKER(&aclrtFreeImplOrigin).stubs().will(invoke(FreeOriginStub));
    MOCKER(&aclrtGetDeviceImplOrigin).stubs().will(invoke(GetDeviceOriginStub));

    DevMemManager &devMemManager = DevMemManager::Instance();

    void *memPtr {};
    ASSERT_EQ(devMemManager.MallocMemory(&memPtr, 100), ACL_SUCCESS);
    ASSERT_NE(memPtr, nullptr);
    devMemManager.Free();
    GlobalMockObject::verify();
}

TEST(DevMemManager, malloc_with_rtMalloc_failed_expect_return_error)
{
    MOCKER(&aclrtMallocImplOrigin).stubs().will(returnValue(ACL_ERROR_BAD_ALLOC));
    MOCKER(&aclrtFreeImplOrigin).stubs().will(returnValue(ACL_SUCCESS));

    DevMemManager &devMemManager = DevMemManager::Instance();

    void *memPtr {};
    ASSERT_NE(devMemManager.MallocMemory(&memPtr, 100), ACL_SUCCESS);
    devMemManager.Free();
    GlobalMockObject::verify();
}

TEST(DevMemManager, malloc_with_smaller_memory_size_expect_reuse_previous_memory)
{
    MOCKER(&aclrtMallocImplOrigin).stubs().will(invoke(MallocOriginStub));
    MOCKER(&aclrtFreeImplOrigin).stubs().will(invoke(FreeOriginStub));
    MOCKER(&aclrtGetDeviceImplOrigin).stubs().will(invoke(GetDeviceOriginStub));

    DevMemManager &devMemManager = DevMemManager::Instance();

    void *memPtr1 {};
    ASSERT_EQ(devMemManager.MallocMemory(&memPtr1, 100), ACL_SUCCESS);
    ASSERT_NE(memPtr1, nullptr);
    void *memPtr2 {};
    ASSERT_EQ(devMemManager.MallocMemory(&memPtr2, 100), ACL_SUCCESS);
    ASSERT_EQ(memPtr1, memPtr2);
    devMemManager.Free();
    GlobalMockObject::verify();
}
