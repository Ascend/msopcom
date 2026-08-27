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


#ifndef __FUNCTION_LOADER_H__
#define __FUNCTION_LOADER_H__

#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>

namespace func_injection {

// FunctionLoader类提供接口的加载能力
// 对于原始接口，可以通过该机制获取符号被使用
// 配合重写同名的函数，并内部调用原始接口符号，实现添加注入函数的可能性
// 约束：
// 1.被劫持的对象需要明确函数修饰,形成隐式依赖
class FunctionLoader {
public:
    explicit FunctionLoader(const std::string& name) noexcept;
    ~FunctionLoader();
    void Set(const std::string& name);
    void* Get(const std::string& name);
private:
    mutable std::mutex mu_;
    std::string fileName;
    void* handle = nullptr;
    mutable std::unordered_map<std::string, void*> registry;
}; // class FunctionLoader


namespace register_function {
class FunctionRegister {
public:
    static FunctionRegister* GetInstance();
    void Register(const std::string& name, ::std::unique_ptr<FunctionLoader>&& ptr);
    void Register(const std::string& name, const std::string& funcName);
    void* Get(const std::string& soName, const std::string& funcName);

private:
    FunctionRegister() = default;
    mutable std::mutex mu_;
    mutable std::unordered_map<std::string, ::std::unique_ptr<FunctionLoader>> registry;
}; // class FunctionRegister


class FunctionRegisterBuilder {
public:
    FunctionRegisterBuilder(const std::string& name, ::std::unique_ptr<FunctionLoader>&& ptr) noexcept;
    FunctionRegisterBuilder(const std::string& soName, const std::string& funcName) noexcept;
}; // class FunctionRegisterBuilder

} // namespace register_function

#define CONCAT_IMPL(x, y) x##y
#define MACRO_CONCAT(x, y) CONCAT_IMPL(x, y)
#define REGISTER_LIBRARY(soName)                                                                        \
    static func_injection::register_function::FunctionRegisterBuilder                                   \
        MACRO_CONCAT(register_library, __COUNTER__)(soName,                                             \
        ::std::unique_ptr<func_injection::FunctionLoader>(new func_injection::FunctionLoader(soName)))

#define REGISTER_FUNCTION(soName, funcName)                                                             \
    static func_injection::register_function::FunctionRegisterBuilder                                   \
        register_function_##funcName(soName, #funcName)

#define GET_FUNCTION(soName, funcName)                                                                  \
    func_injection::register_function::FunctionRegister::GetInstance()->Get(soName, funcName)

} // namespace func_injection
#define FUNC_BODY(soName, funcName, suffix, ErrorRet, ...) \
    using FuncType = decltype(&funcName##suffix); \
    static FuncType funcType = nullptr; \
    if (funcType == nullptr) { \
        funcType = reinterpret_cast<FuncType>(GET_FUNCTION(soName, #funcName)); \
        if (funcType == nullptr) { \
            return ErrorRet; \
        } \
    } \
    return funcType(__VA_ARGS__)
#endif // __FUNCTION_LOADER_H__
