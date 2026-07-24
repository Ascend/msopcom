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


#ifndef __RUNTIME_CORE_HIJACKED_FUNC_TEMPLATE_H__
#define __RUNTIME_CORE_HIJACKED_FUNC_TEMPLATE_H__

#include "core/FunctionLoader.h"
#include "utils/TypeTraits.h"

/** EmptyFuncError 类型类是对 ReturnType 的类型约束
 * @description 用来表示类型可以实例化为错误码，用于表示原始函数获取失败的情况。
 * 你可以通过此类型类进行偏特化，并在结构体中定义代表原始函数获取失败的静态常量
 * `value'，使自定义类型成为 EmptyFuncError 的实例。下面以 `int' 类型为例，对于
 * `int' 类型来说我们以 -1 代表错误码：
 * @begincode
 * template <>
 * struct EmptyFuncError<int> {
 *   static constexpr int VALUE = -1;
 * };
 * @endcode
 */
template <typename ReturnType>
struct EmptyFuncError {};

// generate common type for value
template <>
struct EmptyFuncError<void *> {
    static constexpr void *VALUE = nullptr;
};

// generate common type for value
template <>
struct EmptyFuncError<const char*> {
    static constexpr char *VALUE = nullptr;
};

// HijackedFunc类本身是将被劫持函数变成了一个命令对象，最终将原来调用函数的行为变成调用该类对象的Call函数
// 为了保证参数的正确性，利用模板方法，在编译时检查
// 调用函数失败时，可以通知远端。此处Error可以变成信息通知机制。
template<typename ReturnTypeTagged, typename... Args>
class HijackedFunc {
public:
    // ReturnTypeTagged 类型可能被 TaggedType 包裹，用于 EmptyFuncError 对不同的 Tag 进行
    // 特化处理。originfunc 的返回值类型需要从 ReturnTypeTagged 中解包
    using ReturnType = UnwrapTagT<ReturnTypeTagged>;
    using FuncType = ReturnType (*)(Args...);
    using HijackedFuncType = HijackedFunc<ReturnTypeTagged, Args...>;

    explicit HijackedFunc(const std::string soName, const std::string& funcName)
        : originfunc_{reinterpret_cast<FuncType>(GET_FUNCTION(soName, funcName))} { }
    virtual ~HijackedFunc() {}
    virtual void Pre(Args... args) {}
    virtual ReturnType Post(ReturnType ret)
    {
        return ret;
    }
    virtual ReturnType EmptyFunc() { return EmptyFuncError<ReturnTypeTagged>::VALUE; }
    virtual ReturnType Call(Args... args)
    {
        Pre(args...);
        if (originfunc_) {
            ReturnType ret = originfunc_(args...);
            return Post(ret);
        }
        // 待优化: 需要设计一种异常处理方法
        return EmptyFunc();
    }
    ReturnType OriginCall(Args... args)
    {
        return originfunc_ ? originfunc_(args...) : EmptyFunc();
    }

protected:
    FuncType originfunc_;
}; // class HijackedFunc

// 偏特化，void的处理比较特别(各类函数的声明需要保持一致)
template<typename... Args>
class HijackedFunc<void, Args...> {
public:
    using FuncType = void (*)(Args...);
    using HijackedFuncType = HijackedFunc<void, Args...>;

    explicit HijackedFunc(const std::string soName, const std::string& funcName)
        : originfunc_{reinterpret_cast<FuncType>(GET_FUNCTION(soName, funcName))} { }
    virtual ~HijackedFunc() {}
    virtual void Pre(Args... args) {}
    virtual void Post() {}
    virtual void EmptyFunc() {}
    virtual void Call(Args... args)
    {
        Pre(args...);
        if (originfunc_) {
            originfunc_(args...);
            Post();
        }
    }
protected:
    FuncType originfunc_;
}; // class HijackedFunc

template<typename ReturnType, typename... Args>
auto HijackedFuncHelper(ReturnType (*)(Args...)) -> HijackedFunc<ReturnType, Args...>
{
    return std::declval<HijackedFunc<ReturnType, Args...>>();
}

template<typename ReturnTypeTag, typename ReturnType, typename... Args>
auto HijackedFuncHelperTagged(ReturnType (*)(Args...)) -> HijackedFunc<TaggedType<ReturnType, ReturnTypeTag>, Args...>
{
    return std::declval<HijackedFunc<TaggedType<ReturnType, ReturnTypeTag>, Args...>>();
}

#endif // __RUNTIME_CORE_HIJACKED_FUNC_TEMPLATE_H__
