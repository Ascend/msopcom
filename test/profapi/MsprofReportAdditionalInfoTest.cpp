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

#include "gtest/gtest.h"
#include "mockcpp/mockcpp.hpp"
#define private public
#define protected public
#include "runtime/inject_helpers/ProfConfig.h"
#include "profapi/HijackedFunc.h"
#undef private
#undef protected

#include "core/FuncSelector.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "utils/FileSystem.h"
#include "include/thirdparty/prof.h"
#include "profapi/RegisterMsopprofProfileCallback.h"
/**
 * |  用例集 | MsprofReportAdditionalInfoCallbackImpl
 * | 测试函数 | MsprofReportAdditionalInfoCallbackImpl
 * |  用例名 | write_aicore_timestamp_bin_success
 * | 用例描述 | 测试aicore打点数据写入aic_timestamp.bin成功
 */
TEST(MsprofReportAdditionalInfoCallbackImpl, write_aicore_timestamp_bin_success)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    MOCKER(rtGetDeviceOrigin).stubs().will(returnValue(1));
    std::string path = "./output";
    ASSERT_TRUE(MkdirRecusively(path));
    MOCKER(&GetEnv).stubs().will(returnValue(path));
    MOCKER(&KernelContext::GetMC2Flag).stubs().will(returnValue(true));

    MsprofAdditionalInfo tempdata;
    tempdata.level = 3000;
    tempdata.type = 0;
    VOID_PTR data = &tempdata;
    MsprofReportAdditionalInfoCallbackImpl(1, data, 0);
    ASSERT_TRUE(IsPathExists("./output/device0/aic_timestamp.bin"));
    RemoveAll(path);
    GlobalMockObject::verify();
}

/**
 * |  用例集 | MsprofReportAdditionalInfoCallbackImpl
 * | 测试函数 | MsprofReportAdditionalInfoCallbackImpl
 * |  用例名 | output_path_empty_do_not_write_bim
 * | 用例描述 | 测试output为空无需写入bin文件
 */
TEST(MsprofReportAdditionalInfoCallbackImpl, output_path_empty_do_not_write_bim)
{
    FuncSelector::Instance().Set(ToolType::PROF);
    MOCKER(rtGetDeviceOrigin).stubs().will(returnValue(1));
    MsprofAdditionalInfo tempdata;
    VOID_PTR data = &tempdata;
    MsprofReportAdditionalInfoCallbackImpl(1, data, 0);
    ASSERT_FALSE(IsPathExists("./output/mc2_aic_timestamp.bin"));
    GlobalMockObject::verify();
}

/**
 * |  用例集 | MsprofReportAdditionalInfoCallbackImpl
 * | 测试函数 | MsprofReportAdditionalInfoCallbackImpl
 * |  用例名 | not_opprof_do_not_write_bin
 * | 用例描述 | 测试非OpProf情况下无需写入bin文件
 */
TEST(MsprofReportAdditionalInfoCallbackImpl, not_opprof_do_not_write_bin)
{
    FuncSelector::Instance().Set(ToolType::NONE);
    MOCKER(rtGetDeviceOrigin).stubs().will(returnValue(1));

    MsprofAdditionalInfo tempdata;
    VOID_PTR data = &tempdata;
    MsprofReportAdditionalInfoCallbackImpl(1, data, 0);
    ASSERT_FALSE(IsPathExists("./output/mc2_aic_timestamp.bin"));
    GlobalMockObject::verify();
}
