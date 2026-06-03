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


#include <dlfcn.h>
#include "utils/FileSystem.h"
#include "utils/InjectLogger.h"
#include "core/FunctionLoader.h"
namespace func_injection {

FunctionLoader::FunctionLoader(const std::string &name) noexcept { this->fileName = "lib" + name + ".so"; }

FunctionLoader::~FunctionLoader()
{
    std::lock_guard<std::mutex> lock(mu_);  // 加锁保护临界区
    if (this->handle != nullptr) {
        dlclose(this->handle);
    }
}

void FunctionLoader::Set(const std::string& name)
{
    this->registry.emplace(name, nullptr);
}

void* FunctionLoader::Get(const std::string& name)
{
    std::lock_guard<std::mutex> lock(mu_); // 加锁保护临界区
    if (this->handle == nullptr) {
        // check so valid
        auto soPath = GetSoFromEnvVar(this->fileName);
        if (!CheckInputFileValid(soPath, "so")) {
            ERROR_LOG("Failed to load so %s", this->fileName.c_str());
            return nullptr;
        }
        auto hdl = dlopen(this->fileName.c_str(), RTLD_LAZY);
        if (hdl == nullptr) {
            return nullptr;
        }
        this->handle = hdl;
    }

    auto itr = registry.find(name);
    if (itr == registry.end()) {
        return nullptr;
    }

    if (itr->second != nullptr) {
        return itr->second;
    }

    auto func = dlsym(this->handle, name.c_str());
    if (func == nullptr) {
        return nullptr;
    }
    this->registry[name] = func;
    return func;
}

namespace register_function {
    FunctionRegister* FunctionRegister::GetInstance()
    {
        static FunctionRegister instance;
        return &instance;
    }
    void FunctionRegister::Register(const std::string& name, ::std::unique_ptr<FunctionLoader>&& ptr)
    {
        std::lock_guard<std::mutex> lock(mu_);
        registry.emplace(name, std::move(ptr));
    }

    void FunctionRegister::Register(const std::string& name, const std::string& funcName)
    {
        auto itr = registry.find(name);
        if (itr == registry.end()) {
            return;
        }
        itr->second->Set(funcName);
    }

    void* FunctionRegister::Get(const std::string& soName, const std::string& funcName)
    {
        auto itr = registry.find(soName);
        if (itr != registry.end()) {
            return itr->second->Get(funcName);
        }
        return nullptr;
    }

    FunctionRegisterBuilder::FunctionRegisterBuilder(const std::string& name,
                                                     ::std::unique_ptr<FunctionLoader>&& ptr) noexcept
    {
        FunctionRegister::GetInstance()->Register(name, std::move(ptr));
    }
    FunctionRegisterBuilder::FunctionRegisterBuilder(const std::string& soName,
                                                     const std::string& funcName) noexcept
    {
        FunctionRegister::GetInstance()->Register(soName, funcName);
    }
} // namespace register_function
} // namespace func_injection
