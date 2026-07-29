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


#ifndef __ACL_RUNTIME_CONFIG_H__
#define __ACL_RUNTIME_CONFIG_H__

#include <dlfcn.h>
#include <string>
#include <cstring>
#include <cstdint>
#include <sys/stat.h>
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "utils/Singleton.h"
#include "utils/FileSystem.h"
#include "runtime/inject_helpers/ProfConfig.h"

class AclRuntimeConfig : public Singleton<AclRuntimeConfig, false> {
public:
    static const std::string &GetSoByVersion() {
        auto &inst = AclRuntimeConfig::Instance();
        if (!inst.soName_.empty()) {
            return inst.soName_;
        }
        std::string runtimeSoPath = GetEnv("ASCEND_HOME_PATH") + "/lib64/libruntime.so";
        if (!IsExist(runtimeSoPath)) {
            return GetOldVersion();
        }
        // 用 ELF 解析代替 dlopen + dlsym + dlclose：
        // AclRuntimeLibName() 在 HijackedFunc 全局对象构造时（静态初始化期）被调用，
        // 此时若 dlopen libruntime.so 会提前执行其静态初始化代码并留下副作用，
        // 后续 libruntime.so 被正常加载后状态不一致
        // 直接解析 .dynsym 节只读文件，零副作用。
        if (HasSymbolInSo(runtimeSoPath, "aclrtSetDeviceImpl")) {
            bool isSimulator = (GetEnv(IS_SIMULATOR_ENV) == "true");
            inst.soName_ = isSimulator ? "runtime_camodel" : "runtime";
            return inst.soName_;
        }
        return GetOldVersion();
    }

    static const std::string &GetOldVersion() {
        auto &inst = AclRuntimeConfig::Instance();
        if (!inst.soName_.empty()) {
            return inst.soName_;
        }
        inst.soName_ = "acl_rt_impl";
        // check so valid
        std::string soPath = GetEnv("ASCEND_HOME_PATH");
        if (soPath.empty()) {
            return inst.soName_;
        }
        soPath = soPath + "/lib64/libacl_rt_impl.so";
        if (!IsExist(soPath)) {
            inst.soName_ = "ascendcl_impl";
        }
        return inst.soName_;
    }

private:
    // 不通过 dlopen 加载 SO，而是直接解析 ELF 文件的 .dynsym 节来查符号。
    // 这样在静态初始化期也能安全检查符号是否存在，不会触 SO 的构造函数，
    // 避免 dlopen/dlclose 导致的全局状态副作用。
    static bool HasSymbolInSo(const std::string &soPath, const std::string &symbolName) {
        int fd = open(soPath.c_str(), O_RDONLY);
        if (fd < 0) return false;

        struct stat st;
        if (fstat(fd, &st) < 0) { close(fd); return false; }

        size_t fileSize = st.st_size;
        if (fileSize < sizeof(Elf64_Ehdr)) { close(fd); return false; }

        uint8_t *base = (uint8_t *)mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (base == MAP_FAILED) return false;

        auto *ehdr = (Elf64_Ehdr *)base;
        if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
            munmap(base, fileSize);
            return false;
        }

        // 校验节头表位置不越界
        if (ehdr->e_shoff == 0 || ehdr->e_shnum == 0) { munmap(base, fileSize); return false; }
        size_t shdrEnd;
        if (__builtin_mul_overflow(ehdr->e_shnum, sizeof(Elf64_Shdr), &shdrEnd) ||
            __builtin_add_overflow(ehdr->e_shoff, shdrEnd, &shdrEnd) ||
            shdrEnd > fileSize) {
            munmap(base, fileSize);
            return false;
        }

        auto *shdr = (Elf64_Shdr *)(base + ehdr->e_shoff);
        Elf64_Shdr *dynsym = nullptr;

        for (size_t i = 0; i < ehdr->e_shnum; i++) {
            if (shdr[i].sh_type == SHT_DYNSYM) {
                dynsym = &shdr[i];
                break;
            }
        }
        if (!dynsym) { munmap(base, fileSize); return false; }

        // 校验动态符号表及其字符串表的位置不越界
        size_t symEnd, strEnd;
        if (dynsym->sh_link >= ehdr->e_shnum ||
            __builtin_add_overflow(dynsym->sh_offset, dynsym->sh_size, &symEnd) || symEnd > fileSize ||
            __builtin_add_overflow(shdr[dynsym->sh_link].sh_offset, shdr[dynsym->sh_link].sh_size, &strEnd) ||
            strEnd > fileSize) {
            munmap(base, fileSize);
            return false;
        }

        char *strings = (char *)(base + shdr[dynsym->sh_link].sh_offset);
        auto *symbols = (Elf64_Sym *)(base + dynsym->sh_offset);
        size_t numSymbols = dynsym->sh_size / sizeof(Elf64_Sym);

        for (size_t i = 0; i < numSymbols; i++) {
            // 校验符号名指针在字符串表范围内
            if (symbols[i].st_name >= shdr[dynsym->sh_link].sh_size) {
                continue;
            }
            if (symbolName == (strings + symbols[i].st_name)) {
                munmap(base, fileSize);
                return true;
            }
        }
        munmap(base, fileSize);
        return false;
    }

protected:
    std::string soName_;
};

inline const std::string &AclRuntimeLibName() { return AclRuntimeConfig::GetSoByVersion(); }

#endif // __RUNTIME_CONFIG_H__
