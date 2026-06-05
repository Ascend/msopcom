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


#ifndef __CORE_FUNC_SELECTOR_H__
#define __CORE_FUNC_SELECTOR_H__

#include "utils/Singleton.h"

enum class ToolType {
    NONE,
    PROF,
    SANITIZER,
    TEST        // 测试使用
};

// FuncSelector类本身针对不同的工具，支持使能不同的分支
// 通过与Bind下的文件绑定，实现特定的注入函数的选择
class FuncSelector : public Singleton<FuncSelector, false> {
    friend class Singleton<FuncSelector, false>;
public:
    void Set(ToolType toolType);
    // For Test
    bool Reset(ToolType toolType);
    bool Match(ToolType toolType) const;
private:
    explicit FuncSelector() {}

private:
    ToolType type_{ToolType::NONE};
    ToolType subType_{ToolType::NONE}; // 仅在type_==TEST下生效
};

bool IsSanitizer();
bool IsOpProf();

#endif // __CORE_FUNC_SELECTOR_H__
