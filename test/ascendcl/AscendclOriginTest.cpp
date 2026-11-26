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
#include "ascendcl/AscendclOrigin.h"
#include "mockcpp/mockcpp.hpp"

TEST(AscendclOrigin, ascendcl_function_load_failed)
{
    MOCKER(&func_injection::register_function::FunctionRegister::Get)
        .stubs()
        .will(returnValue((void*)nullptr));
    AscendclOriginCtor();
    EXPECT_EQ(aclrtGetUserDevIdByPhyDevIdOrigin(2, nullptr), ACL_ERROR_INTERNAL_ERROR);
    EXPECT_NE(aclrtSetOpExecuteTimeOutOrigin(0), ACL_ERROR_NONE);
    EXPECT_NE(aclrtCreateEventOrigin(nullptr), ACL_ERROR_NONE);
    EXPECT_NE(aclrtStreamWaitEventOrigin(nullptr, nullptr), ACL_ERROR_NONE);
    EXPECT_NE(aclrtRecordEventOrigin(nullptr, nullptr), ACL_ERROR_NONE);
    EXPECT_NE(aclrtResetEventOrigin(nullptr, nullptr), ACL_ERROR_NONE);
    EXPECT_NE(aclrtDestroyEventOrigin(nullptr), ACL_ERROR_NONE);
    GlobalMockObject::verify();
}

// 验证 Origin 按公开 ACL 符号查找，并将物理卡 2 转为进程内设备 1。
TEST(AscendclOrigin, get_user_device_id_by_phy_device_id_forwards_call) {
    using func_injection::register_function::FunctionRegister;
    const auto load = +[](FunctionRegister *, const std::string &library, const std::string &symbol) -> void * {
        EXPECT_EQ(library, "ascendcl");
        EXPECT_EQ(symbol, "aclrtGetUserDevIdByPhyDevId");
        const auto convert = +[](int32_t phyDeviceId, int32_t *userDeviceId) -> aclError {
            EXPECT_EQ(phyDeviceId, 2);
            *userDeviceId = 1;
            return ACL_SUCCESS;
        };
        return reinterpret_cast<void *>(convert);
    };
    MOCKER(&FunctionRegister::Get).expects(once()).will(invoke(load));

    int32_t userDeviceId = -1;
    EXPECT_EQ(aclrtGetUserDevIdByPhyDevIdOrigin(2, &userDeviceId), ACL_SUCCESS);
    EXPECT_EQ(userDeviceId, 1);
    GlobalMockObject::verify();
}
