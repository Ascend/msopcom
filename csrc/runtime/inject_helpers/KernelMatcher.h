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

#pragma once

#include <cstdint>
#include <string>

/*
 * KernelMatcher is used to match launch event.
 * Current only support match launch event by launch id or kernel name.
 *
 * Future:
 * 1. support match by multiple launch id range.
 * 2. support kernel name fused match.
 * 3. move to new file to become common
 */
class KernelMatcher {
public:
    // Match all kernel if config is all default.
    // Match success if launchId or kernelName is the same
    // The priority of match: launchId > kernelName
    struct Config {
        uint64_t launchId{UINT64_MAX}; // start from 0
        uint64_t regId{UINT64_MAX};
        std::string kernelName{};
    };

    explicit KernelMatcher(Config config) : config_{std::move(config)} { }

    // Return true if match success
    // Added fuse match in future
    bool Match(uint64_t launchId, const std::string &kernelName) const;
private:
    bool IsMatchAll() const;

private:
    Config config_;
};
