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

#include <gtest/gtest.h>
#define private public
#define protected public
#include "ascend_dump/HijackedFunc.h"
#undef private
#undef protected
#include "runtime/inject_helpers/ArgsManager.h"

namespace {
constexpr uint64_t STATIC_ATOMIC_INDEX = 0xA5A5A5A580000000ULL;
} // namespace

TEST(AdumpGetDFXInfoAddrForStatic, test_call_function_without_originfunc)
{
    auto &argsManager = ArgsManager::Instance();
    argsManager.Clear();

    uint32_t space = 1;
    uint64_t atomicIndex = STATIC_ATOMIC_INDEX;

    HijackedFuncOfAdumpGetDFXInfoAddrForStatic staticHook;
    staticHook.originfunc_ = nullptr;
    EXPECT_EQ(staticHook.Call(space, atomicIndex), nullptr);
    EXPECT_EQ(atomicIndex, STATIC_ATOMIC_INDEX);
    EXPECT_FALSE(argsManager.HasAdumpInfo());

    argsManager.Clear();
}

TEST(AdumpGetDFXInfoAddrForStatic, test_call_function_with_originfunc_and_check_expect_return)
{
    auto &argsManager = ArgsManager::Instance();
    argsManager.Clear();

    uint32_t space = 11;
    uint64_t atomicIndex = 0;

    HijackedFuncOfAdumpGetDFXInfoAddrForStatic staticHook;
    staticHook.originfunc_ = [](uint32_t, uint64_t &index) -> void * {
        index = STATIC_ATOMIC_INDEX;
        return nullptr;
    };
    EXPECT_EQ(staticHook.Call(space, atomicIndex), nullptr);
    EXPECT_EQ(atomicIndex, STATIC_ATOMIC_INDEX);

    AdumpInfo info{};
    EXPECT_TRUE(argsManager.GetAdumpInfo(STATIC_ATOMIC_INDEX, info));
    EXPECT_EQ(info.addr, nullptr);
    EXPECT_EQ(info.argsSpace, space);

    argsManager.Clear();
}
