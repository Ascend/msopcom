/* -------------------------------------------------------------------------
 *  This file is part of the MindStudio project.
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

#ifndef MSOPSCOMMON_TEST_CORE_SOC_RESOLUTION_CASES_H
#define MSOPSCOMMON_TEST_CORE_SOC_RESOLUTION_CASES_H

#include "core/PlatformConfig.h"

// common 与工具侧 UT 共用的输入及期望，避免各仓独立修改相反的预期。
namespace SocResolutionTestData {

struct TestCase {
    const char *soc;
    ::ChipProductType product;
    ::DeviceType device;
};

const TestCase RESOLVED_SOC_CASES[] = {
    {"Ascend910B3", ::ChipProductType::ASCEND910B3, ::DeviceType::ASCEND_910B3},
    {"Ascend910B4", ::ChipProductType::ASCEND910B4, ::DeviceType::ASCEND_910B4},
    {"Ascend910B4-1", ::ChipProductType::ASCEND910B4_1, ::DeviceType::ASCEND_910B4},
    {"Ascend910B9", ::ChipProductType::ASCEND910B_SERIES, ::DeviceType::ASCEND_910B_DEFAULT},
    {"Ascend910B9999", ::ChipProductType::ASCEND910B_SERIES, ::DeviceType::ASCEND_910B_DEFAULT},
    {"Ascend910_9399", ::ChipProductType::ASCEND910_93_SERIES, ::DeviceType::ASCEND_910B_DEFAULT},
    {"Ascend910_93xx", ::ChipProductType::ASCEND910_93_SERIES, ::DeviceType::ASCEND_910B_DEFAULT},
    {"Ascend950PR_9589", ::ChipProductType::ASCEND950PR_9589, ::DeviceType::ASCEND_950PR_9589},
    {"Ascend950PR_958x", ::ChipProductType::ASCEND950_SERIES, ::DeviceType::ASCEND_950_DEFAULT},
    {"Ascend950PR_959x", ::ChipProductType::ASCEND950_SERIES, ::DeviceType::ASCEND_950_DEFAULT},
    {"Ascend950DT_959x", ::ChipProductType::ASCEND950_SERIES, ::DeviceType::ASCEND_950_DEFAULT},
    {"Ascend950PR_95A9", ::ChipProductType::ASCEND950_SERIES, ::DeviceType::ASCEND_950_DEFAULT},
    {"Ascend950DT_95Z9", ::ChipProductType::ASCEND950_SERIES, ::DeviceType::ASCEND_950_DEFAULT},
    {"AscendPremiumA", ::ChipProductType::ASCEND910PREMIUMA, ::DeviceType::ASCEND_910_PREMIUM_A},
    {"Ascend910PremiumA", ::ChipProductType::ASCEND910PREMIUMA, ::DeviceType::ASCEND_910_PREMIUM_A},
    {"Ascend310P1", ::ChipProductType::ASCEND310P1, ::DeviceType::ASCEND_310P},
    // 裸 Ascend910B 是旧 910A 产品；可识别产品，但不映射为 910B DEFAULT。
    {"Ascend910B", ::ChipProductType::ASCEND910B, ::DeviceType::INVALID},
};

const char *const INVALID_SOC_NAMES[] = {
    "",
    "Ascend910Bfoo",
    "Ascend910_93",
    "Ascend910_93x",
    "Ascend910_93999",
    "Ascend910_93foo",
    "Ascend910_939-",
    "Ascend910_93_1",
    "Ascend910_94xx",
    "Ascend950DT_",
    "Ascend950PR_",
    "Ascend950DT_95",
    "Ascend950PR_95",
    "Ascend950DT_95x",
    "Ascend950PR_95x",
    "Ascend950DT_95999",
    "Ascend950PR_95999",
    "Ascend950DT_96xx",
    "Ascend950PR_96xx",
    "Ascend950DT_95_1",
    "Ascend950PR_959-",
    "Ascend950PR_95 x",
    "Ascend950DT_95\xC3\xA9",
    "Ascend950DT_9582x",
    "Ascend950PR_957bx",
    "Ascend9500PR_959x",
    "OtherAscend950PR_959x",
};

} // namespace SocResolutionTestData

#endif // MSOPSCOMMON_TEST_CORE_SOC_RESOLUTION_CASES_H
