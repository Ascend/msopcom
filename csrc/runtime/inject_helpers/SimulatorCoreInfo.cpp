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

#include "runtime/inject_helpers/SimulatorCoreInfo.h"

#include "include/opprof/SimulatorSocConfig.h"
#include "runtime/inject_helpers/KernelContext.h"

namespace SimulatorSoc {

bool TryGetSimulatorAiCoreNum(const std::string &socVersion, uint32_t &coreNum) {
    const auto iter = CORE_NUM.find(socVersion);
    if (iter != CORE_NUM.end()) {
        coreNum = iter->second;
        return true;
    }
    return GetA5SimulatorCoreNum(socVersion, coreNum);
}

} // namespace SimulatorSoc
