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
#include "hccl/HijackedFunc.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/KernelContext.h"


TEST(HijackedFuncOfHcclCommInitRootInfo, get_hccl_comm_normal_when_type_is_prof)
{
    KernelContext::Instance().SetHcclComm(nullptr);
    FuncSelector::Instance().Set(ToolType::PROF);
    HijackedFuncOfHcclCommInitRootInfo instance;
    HcclRootInfo rootInfo;
    HcclComm comm;
    instance.Pre(1, &rootInfo, 1, &comm);
    instance.Post(HCCL_SUCCESS);
    ASSERT_EQ(KernelContext::Instance().GetHcclComm(), comm);
}

TEST(HijackedFuncOfHcclCommInitRootInfo, get_hccl_comm_null_when_type_is_not_prof)
{
    KernelContext::Instance().SetHcclComm(nullptr);
    FuncSelector::Instance().Set(ToolType::TEST);
    HijackedFuncOfHcclCommInitRootInfo instance;
    HcclRootInfo rootInfo;
    HcclComm comm;
    instance.Pre(1, &rootInfo, 1, &comm);
    instance.Post(HCCL_SUCCESS);
    ASSERT_TRUE(KernelContext::Instance().GetHcclComm() == nullptr);
}
