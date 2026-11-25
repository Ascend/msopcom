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

// 该文件主要实现注入函数的功能，其配合被劫持函数的别名，实现新的劫持函数功能

#include "HijackedFunc.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "utils/InjectLogger.h"

HijackedFuncOfAclrtMemcpy2dImpl::HijackedFuncOfAclrtMemcpy2dImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtMemcpy2dImpl") {}

void HijackedFuncOfAclrtMemcpy2dImpl::Pre(void *dst, size_t dpitch, const void *src, size_t spitch,
                                          size_t width, size_t height, aclrtMemcpyKind kind)
{
    if (height > MAX_MEMORY_RECORD_HEIGHT) {
        WARN_LOG("Height truncated from %zu to %zu for AclrtMemcpy2d", height, MAX_MEMORY_RECORD_HEIGHT);
        height = MAX_MEMORY_RECORD_HEIGHT;
    }
    if (IsSanitizer()) {
        ReportAclMemcpy(dst, src, width, kind, dpitch, spitch, height);
    }
}
