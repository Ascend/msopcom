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
#include "ascend_hal/AscendHalOrigin.h"

TEST(AscendHalOrigin, test_call_function_with_originfunc_and_check_expect_return)
{
    auto ret1 = prof_drv_start_origin(0, 0, {});
    EXPECT_EQ(ret1, -1);
    auto ret2 = prof_channel_read_origin(0, 0, {}, {});
    EXPECT_EQ(ret2, -1);
    auto ret3 = prof_stop_origin(0, 0);
    EXPECT_EQ(ret3, -1);
    auto ret4 = prof_channel_poll_origin({}, 0, 0);
    EXPECT_EQ(ret4, -1);
    auto ret5 = halGetDeviceInfoByBuffOrigin(0, 0, 0, 0, {});
    EXPECT_EQ(ret5, DRV_ERROR_RESERVED);
    auto ret6 = halGetDeviceInfoOrigin(0, 0, 0, {});
    EXPECT_EQ(ret6, DRV_ERROR_RESERVED);
    int halApiVersion = 0;
    auto ret7 = halGetAPIVersionOrigin(&halApiVersion);
    EXPECT_EQ(ret7, DRV_ERROR_NONE);
    EXPECT_EQ(halApiVersion, 0x072419);
    auto ret8 = halGetAPIVersionOrigin(nullptr);
    EXPECT_EQ(ret8, DRV_ERROR_RESERVED);
}
