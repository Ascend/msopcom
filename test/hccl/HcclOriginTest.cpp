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
#include "hccl/HcclOrigin.h"
#include "core/FunctionLoader.h"
#include "mockcpp/mockcpp.hpp"

TEST(HcclOrigin, hccl_function_load_failed)
{
    MOCKER(&func_injection::register_function::FunctionRegister::Get)
        .stubs()
        .will(returnValue((void*)nullptr));

    EXPECT_NE(HcclBarrierOrigin(nullptr, nullptr), HCCL_SUCCESS);
    GlobalMockObject::verify();
}

// 测试场景：CANN版本未提供通信域或包间窗口查询符号
// 测试预期：原接口加载失败返回错误，不把未知窗口视为成功
TEST(HcclOrigin, optional_window_symbols_missing) {
    MOCKER(&func_injection::register_function::FunctionRegister::Get).stubs().will(returnValue((void *)nullptr));
    uint32_t rankSize = 0;
    uint64_t size = 0;
    void *addr = nullptr;
    EXPECT_NE(HcclGetCommNameOrigin(nullptr, nullptr), HCCL_SUCCESS);
    EXPECT_NE(HcclGetRankSizeOrigin(nullptr, &rankSize), HCCL_SUCCESS);
    EXPECT_NE(CommGetRemoteCCLBufOrigin(nullptr, 0, &addr, &size), HCCL_SUCCESS);
    EXPECT_NE(HcclCommDestroyOrigin(nullptr), HCCL_SUCCESS);
    GlobalMockObject::verify();
}
