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

#ifndef MSOPPROF_RUNTIME_INJECT_HELPERS_SIMULATOR_CORE_INFO_H_
#define MSOPPROF_RUNTIME_INJECT_HELPERS_SIMULATOR_CORE_INFO_H_

#include <cstdint>
#include <string>

namespace SimulatorSoc {

// Shared A2/A3 and A5 lookup. The 310P override remains specific to rtGetAiCoreCount.
bool TryGetSimulatorAiCoreNum(const std::string &socVersion, uint32_t &coreNum);

} // namespace SimulatorSoc

#endif // MSOPPROF_RUNTIME_INJECT_HELPERS_SIMULATOR_CORE_INFO_H_
