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

#ifndef __ASCEND_DUMP_HIJACKED_FUNC_H__
#define __ASCEND_DUMP_HIJACKED_FUNC_H__

#include <memory>
#include "ascend_dump.h"
#include "core/HijackedFuncTemplate.h"
class HijackedFuncOfAdumpGetDFXInfoAddrForDynamic
    : public decltype(HijackedFuncHelper(&AdumpGetDFXInfoAddrForDynamic)) {
public:
    explicit HijackedFuncOfAdumpGetDFXInfoAddrForDynamic();
    void *Call(uint32_t space, uint64_t & atomicIndex) override;

}; // class HijackedFuncOfAdumpGetDFXInfoAddrForDynamic

#endif //__ASCEND_DUMP_HIJACKED_FUNC_H__
