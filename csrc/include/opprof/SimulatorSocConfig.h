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
#ifndef MSOPPROF_INCLUDE_OPPROF_SIMULATOR_SOC_CONFIG_H_
#define MSOPPROF_INCLUDE_OPPROF_SIMULATOR_SOC_CONFIG_H_

#include <cstdint>
#include <string>

#include "core/PlatformConfig.h"
#include "utils/Ustring.h"

namespace SimulatorSoc {

inline bool MatchesA5Family(const std::string &socVersion, const std::string &family) {
    return StartsWith(socVersion, "Ascend950PR_" + family) || StartsWith(socVersion, "Ascend950DT_" + family);
}

/*
 * 已登记的具体 SoC 保持原 ChipProductType。
 * 新增后缀映射到同 PR/DT、同系列的代表型号，供仿真任务和解析器判断产品系列。
 */
inline ChipProductType GetSimulatorProductTypeBySocVersion(const std::string &socVersion) {
    ChipProductType exactType = ::GetProductTypeBySocVersion(socVersion);
    if (exactType != ChipProductType::UNKNOWN_PRODUCT_TYPE) {
        return exactType;
    }

    const bool isPr = StartsWith(socVersion, "Ascend950PR_");

    if (MatchesA5Family(socVersion, "957")) {
        return isPr ? ChipProductType::ASCEND950PR_9579 : ChipProductType::ASCEND950DT_9571;
    }
    if (MatchesA5Family(socVersion, "958")) {
        return isPr ? ChipProductType::ASCEND950PR_9589 : ChipProductType::ASCEND950DT_9581;
    }
    if (MatchesA5Family(socVersion, "959")) {
        return isPr ? ChipProductType::ASCEND950PR_9599 : ChipProductType::ASCEND950DT_9591;
    }

    return ChipProductType::UNKNOWN_PRODUCT_TYPE;
}

inline bool IsSupportedA5SimulatorSocVersion(const std::string &socVersion) {
    ChipProductType productType = GetSimulatorProductTypeBySocVersion(socVersion);
    return ::GetProductSeriesType(productType) == ChipProductType::ASCEND950_SERIES;
}

inline bool GetA5SimulatorCoreNum(const std::string &socVersion, uint32_t &coreNum) {
    if (MatchesA5Family(socVersion, "957")) {
        coreNum = 28;
        return true;
    }
    if (MatchesA5Family(socVersion, "958")) {
        coreNum = 32;
        return true;
    }
    if (MatchesA5Family(socVersion, "959")) {
        coreNum = 36;
        return true;
    }

    // 保留现有特殊型号。
    if (socVersion == "Ascend950DT_950x" || socVersion == "Ascend950DT_950y" || socVersion == "Ascend950PR_950z") {
        coreNum = 8;
        return true;
    }
    if (socVersion == "Ascend950DT_95A1" || socVersion == "Ascend950DT_95A2") {
        coreNum = 36;
        return true;
    }

    return false;
}

} // namespace SimulatorSoc

#endif // MSOPPROF_INCLUDE_OPPROF_SIMULATOR_SOC_CONFIG_H_
