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

// 该文件定义劫持函数对象
// 这个文件中尽量不要引入各种私有定义，保持依赖的干净

#ifndef __ASCENDCL_HIJACKED_FUNC_H__
#define __ASCENDCL_HIJACKED_FUNC_H__

#include "acl.h"
#include "core/HijackedFuncTemplate.h"

template <> struct EmptyFuncError<aclError> {
    // `ACL_ERROR_INTERNAL_ERROR' 用于代表 aclError 类型中原始函数获取失败
    static constexpr aclError VALUE = ACL_ERROR_INTERNAL_ERROR;
};

#endif // __ASCENDCL_HIJACKED_FUNC_H__
