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

#ifndef __ASCEND_HAL_HIJACKED_FUNC_H__
#define __ASCEND_HAL_HIJACKED_FUNC_H__

#include <memory>
#include "ascend_hal.h"
#include "core/HijackedFuncTemplate.h"

template <> struct EmptyFuncError<drvError_t> {
    // `DRV_ERROR_RESERVED' 用于代表 drvError_t 类型中原始函数获取失败
    static constexpr drvError_t VALUE = DRV_ERROR_RESERVED;
};

class HijackedFuncOfHalGetSocVersion : public decltype(HijackedFuncHelper(&halGetSocVersion)) {
public:
    explicit HijackedFuncOfHalGetSocVersion();
    drvError_t Call(uint32_t devId, char *version, const uint32_t maxLen) override;
};

#endif //__ASCEND_HAL_HIJACKED_FUNC_H__
