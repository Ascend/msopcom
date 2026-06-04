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

#ifndef __PROF_HIJACKED_FUNC_H__
#define __PROF_HIJACKED_FUNC_H__

#include "include/thirdparty/prof.h"
#include "core/HijackedFuncTemplate.h"

class ProfErrorTag;

template <> struct EmptyFuncError<TaggedType<int32_t, ProfErrorTag>> {
    static constexpr int32_t VALUE = 500000;
};

template <typename ReturnType, typename... Args>
auto ProfHijackedType(ReturnType (*func)(Args...)) -> HijackedFunc<TaggedType<ReturnType, ProfErrorTag>, Args...> {
    return HijackedFuncHelperTagged<ProfErrorTag>(func);
}

class HijackedFuncOfMsprofRegisterCallback : public decltype(ProfHijackedType(&MsprofRegisterCallback)) {
public:
    explicit HijackedFuncOfMsprofRegisterCallback();
    ~HijackedFuncOfMsprofRegisterCallback() override = default;
    void Pre(uint32_t moduleId, ProfCommandHandle handle) override;
};

class HijackedFuncOfMsprofNotifySetDevice : public decltype(ProfHijackedType(&MsprofNotifySetDevice)) {
public:
    explicit HijackedFuncOfMsprofNotifySetDevice();
    ~HijackedFuncOfMsprofNotifySetDevice() override = default;
    void Pre(uint32_t chipId, uint32_t deviceId, bool isOpen) override;
};

#endif // __PROF_HIJACKED_FUNC_H__
