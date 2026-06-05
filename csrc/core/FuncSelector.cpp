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


#include "FuncSelector.h"
#include "utils/InjectLogger.h"

void FuncSelector::Set(ToolType toolType)
{
    this->type_ = toolType;
}

bool FuncSelector::Reset(ToolType toolType)
{
    if (this->type_ != ToolType::TEST) {
        return false;
    }
    this->subType_ = toolType;
    return true;
}

bool FuncSelector::Match(ToolType toolType) const
{
    if (this->type_ == ToolType::NONE) {
        ERROR_LOG("ToolType is None.");
        return false;
    }
    if (this->type_ == ToolType::TEST) {
        return this->subType_ == toolType;
    }

    return this->type_ == toolType;
}

bool IsOpProf()
{
    return FuncSelector::Instance().Match(ToolType::PROF);
}

bool IsSanitizer()
{
    return FuncSelector::Instance().Match(ToolType::SANITIZER);
}
