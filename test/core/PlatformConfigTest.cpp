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

#include "mockcpp/mockcpp.hpp"
#include "core/PlatformConfig.h"
#include "SocResolutionCases.h"

namespace {

class SocFallbackTest : public testing::Test {
protected:
    void TearDown() override { GlobalMockObject::verify(); }
};

TEST_F(SocFallbackTest, resolves_exact_and_prefix_soc) {
    for (const auto &testCase : SocResolutionTestData::RESOLVED_SOC_CASES) {
        SCOPED_TRACE(testCase.soc);
        EXPECT_EQ(testCase.product, GetProductTypeBySocVersion(testCase.soc));
        EXPECT_EQ(testCase.device, GetDeviceTypeBySocVersion(testCase.soc));
    }
    EXPECT_EQ(ChipProductType::ASCEND910A_SERIES, GetProductSeriesType("Ascend910B"));
}

TEST_F(SocFallbackTest, rejects_unknown_or_incomplete_prefixes) {
    for (const auto *soc : SocResolutionTestData::INVALID_SOC_NAMES) {
        SCOPED_TRACE(soc);
        EXPECT_EQ(ChipProductType::UNKNOWN_PRODUCT_TYPE, GetProductTypeBySocVersion(soc));
        EXPECT_EQ(DeviceType::INVALID, GetDeviceTypeBySocVersion(soc));
    }
}

TEST_F(SocFallbackTest, denylist_blocks_fallback_but_preserves_exact_soc) {
    // 模拟名单命中，不修改正式的空名单。
    using Denylist = std::unordered_set<std::string>;
    MOCKER_CPP(&Denylist::count).stubs().will(returnValue(static_cast<Denylist::size_type>(1)));

    EXPECT_EQ(ChipProductType::ASCEND910B3, GetProductTypeBySocVersion("Ascend910B3"));
    EXPECT_EQ(DeviceType::ASCEND_910B3, GetDeviceTypeBySocVersion("Ascend910B3"));
    EXPECT_EQ(ChipProductType::ASCEND910B4, GetProductTypeBySocVersion("Ascend910B4"));
    EXPECT_EQ(DeviceType::ASCEND_910B4, GetDeviceTypeBySocVersion("Ascend910B4"));
    for (const auto *soc : {"Ascend910B9999", "Ascend910B9", "Ascend950PR_958x"}) {
        SCOPED_TRACE(soc);
        EXPECT_EQ(ChipProductType::UNKNOWN_PRODUCT_TYPE, GetProductTypeBySocVersion(soc));
        EXPECT_EQ(DeviceType::INVALID, GetDeviceTypeBySocVersion(soc));
    }
}

} // namespace
