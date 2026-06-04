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


#include <dlfcn.h>

#include <gtest/gtest.h>
#include "kernel_injection/include/MSBit.h"
#include "mockcpp/mockcpp.hpp"
#include "utils/FileSystem.h"

using namespace std;

extern "C" {
void MSBitStart(const char *output, uint16_t length, const char *archName);
}

TEST(MSBit, bind_probe_func_then_start_expect_gen_ctrl_bin_file)
{
    Bind(InstrType::COPY_GM_TO_UBUF, "probe_function1", {0, 1, 2});
    string outputPath = "./tmp_ctrl.bin";
    MSBitStart(outputPath.c_str(), outputPath.size(), nullptr);
    ASSERT_TRUE(IsPathExists(outputPath));
    RemoveAll(outputPath);
}
