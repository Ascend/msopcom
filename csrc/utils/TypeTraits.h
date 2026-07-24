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


#pragma once

#include <type_traits>


template <typename T>
using add_const_t = typename std::add_const<T>::type;

template <typename T>
constexpr add_const_t<T>& as_const(T &t) noexcept
{
    return t;
}

/**
 * 通过 Tag 为类型 T 生成不同的类型包装，以实现对相同类型生成不同的重载函数。
 *
 * 如我们有一个类 A 包含 int 型的成员 a 和 b，希望生成两个构造函数分别对 a 和
 * b 进行初始化。因成员 a 和 b 具有相同类型，无法通过类型来实现构造函数的重载，
 * 可以借助 TaggedType 将 int 类型打上标签来实现：
 * class A {
 *     int a;
 *     int b;
 *     using VA = TaggedType<int, struct ATag>;
 *     using VB = TaggedType<int, struct BTag>;
 *     A(VA const & a) : a{a.value} { }
 *     A(VB const & b) : b{b.value} { }
 * };
 */
template <typename T, typename Tag>
struct TaggedType {
    T value;
};

/**
 * 对 TaggedType 类型进行解包，返回包裹的类型；未被包裹的类型返回传入的类型本身
 */
template <typename T>
struct UnwrapTag {
    using type = T;
};

template <typename T, typename Tag>
struct UnwrapTag<TaggedType<T, Tag>> {
    using type = T;
};

template <typename T>
using UnwrapTagT = typename UnwrapTag<T>::type;

template <typename EnumT>
auto EnumToUnderlying(EnumT e) -> typename std::underlying_type<EnumT>::type
{
    using underlyingT = typename std::underlying_type<EnumT>::type;
    return static_cast<underlyingT>(e);
}
