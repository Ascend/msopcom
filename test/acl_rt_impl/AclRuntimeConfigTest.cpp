/* -------------------------------------------------------------------------
 *  This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
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

#include <gtest/gtest.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cstring>
#include "mockcpp/mockcpp.hpp"
#include "utils/FileSystem.h"
#include "runtime/inject_helpers/ProfConfig.h"

#define private public
#define protected public
#include "acl_rt_impl/AclRuntimeConfig.h"
#undef private
#undef protected

using namespace std;

// RAII helper: 析构时自动 unlink 文件，即使断言失败也能保证清理
struct FileGuard {
    explicit FileGuard(string p) : path(move(p)) {}
    ~FileGuard() {
        if (!path.empty()) {
            unlink(path.c_str());
        }
    }
    FileGuard(const FileGuard &) = delete;
    FileGuard &operator=(const FileGuard &) = delete;
    string path;
};

// RAII helper: 创建临时目录，析构时递归清理
struct TempDirGuard {
    explicit TempDirGuard(const string &tpl) {
        path = tpl + ".XXXXXX";
        if (mkdtemp(&path[0])) {
            ok_ = true;
        }
    }
    ~TempDirGuard() {
        if (ok_) {
            string lib64 = path + "/lib64";
            string soFile = lib64 + "/libacl_rt_impl.so";
            string runtimeSo = lib64 + "/libruntime.so";
            unlink(soFile.c_str());
            unlink(runtimeSo.c_str());
            rmdir(lib64.c_str());
            rmdir(path.c_str());
        }
    }
    TempDirGuard(const TempDirGuard &) = delete;
    TempDirGuard &operator=(const TempDirGuard &) = delete;
    string path;
    bool ok_{false};
};

namespace {

// 构造一个最小 ELF64 .so 文件，其 .dynsym 节中包含 symbolName
bool CreateTestElfSo(const string &soPath, const string &symbolName) {
    int fd = open(soPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }

    const char shstrtabData[] = "\0.shstrtab\0.dynsym\0.strtab";
    size_t shstrtabLen = sizeof(shstrtabData);

    string strtabContent = string("\0", 1) + symbolName + "\0";
    size_t strtabLen = strtabContent.size();
    size_t symEntrySize = sizeof(Elf64_Sym);
    size_t dynsymLen = 2 * symEntrySize;

    size_t ehdrSize = sizeof(Elf64_Ehdr);
    size_t shdrEntrySize = sizeof(Elf64_Shdr);
    size_t shdrCount = 3;
    size_t shdrSize = shdrCount * shdrEntrySize;

    size_t shstrtabOff = ehdrSize + shdrSize;
    size_t dynsymOff = shstrtabOff + shstrtabLen;
    size_t strtabOff = dynsymOff + dynsymLen;

    Elf64_Ehdr ehdr = {};
    memcpy(ehdr.e_ident, ELFMAG, SELFMAG);
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_type = ET_DYN;
    ehdr.e_machine = EM_AARCH64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_shoff = ehdrSize;
    ehdr.e_shentsize = shdrEntrySize;
    ehdr.e_shnum = shdrCount;
    ehdr.e_shstrndx = 0;

    Elf64_Shdr shdr[3] = {};
    shdr[0].sh_name = 1;
    shdr[0].sh_type = SHT_STRTAB;
    shdr[0].sh_offset = shstrtabOff;
    shdr[0].sh_size = shstrtabLen;

    shdr[1].sh_name = 11;
    shdr[1].sh_type = SHT_DYNSYM;
    shdr[1].sh_offset = dynsymOff;
    shdr[1].sh_size = dynsymLen;
    shdr[1].sh_link = 2;
    shdr[1].sh_info = 1;
    shdr[1].sh_addralign = 8;
    shdr[1].sh_entsize = symEntrySize;

    shdr[2].sh_name = 20;
    shdr[2].sh_type = SHT_STRTAB;
    shdr[2].sh_offset = strtabOff;
    shdr[2].sh_size = strtabLen;

    Elf64_Sym sym[2] = {};
    sym[1].st_name = 1;
    sym[1].st_info = STB_GLOBAL | STT_FUNC;

    auto writeAll = [fd](const void *buf, size_t size) { return write(fd, buf, size) == static_cast<ssize_t>(size); };

    bool ok = writeAll(&ehdr, sizeof(ehdr)) && writeAll(shdr, sizeof(shdr)) && writeAll(shstrtabData, shstrtabLen) &&
        writeAll(sym, sizeof(sym)) && writeAll(strtabContent.data(), strtabLen);

    close(fd);
    if (!ok) {
        unlink(soPath.c_str());
    }
    return ok;
}
}

// SetUp/TearDown 保存并恢复 ASCEND_HOME_PATH 和 IS_SIMULATOR_ENV
class AclRuntimeConfigTest : public testing::Test {
    void SetUp() override {
        const char *home = ::getenv("ASCEND_HOME_PATH");
        if (home) {
            savedHomePath_ = home;
        }
        const char *sim = ::getenv("IS_SIMULATOR_ENV");
        if (sim) {
            savedSimulatorEnv_ = sim;
        }
    }
    void TearDown() override {
        AclRuntimeConfig::Instance().soName_.clear();
        if (savedHomePath_.empty()) {
            ::unsetenv("ASCEND_HOME_PATH");
        } else {
            ::setenv("ASCEND_HOME_PATH", savedHomePath_.c_str(), 1);
        }
        if (savedSimulatorEnv_.empty()) {
            ::unsetenv("IS_SIMULATOR_ENV");
        } else {
            ::setenv("IS_SIMULATOR_ENV", savedSimulatorEnv_.c_str(), 1);
        }
        GlobalMockObject::verify();
    }
    string savedHomePath_;
    string savedSimulatorEnv_;
};

// ---------- GetOldVersion ----------

// libacl_rt_impl.so 存在 → GetOldVersion 返回 "acl_rt_impl"
TEST_F(AclRuntimeConfigTest, GetOldVersion_returns_acl_rt_impl_when_so_exists) {
    TempDirGuard dir("/tmp/ut_aclruntime");
    ASSERT_TRUE(dir.ok_);
    string lib64 = dir.path + "/lib64";
    ASSERT_EQ(::mkdir(lib64.c_str(), 0755), 0);
    string soFile = lib64 + "/libacl_rt_impl.so";
    FileGuard soGuard(soFile);
    {
        int fd = ::open(soFile.c_str(), O_CREAT | O_WRONLY, 0644);
        if (fd >= 0) {
            ::close(fd);
        }
    }
    MOCKER(&GetEnv).stubs().will(returnValue(dir.path));
    ASSERT_EQ(AclRuntimeConfig::GetOldVersion(), "acl_rt_impl");
}

// libacl_rt_impl.so 不存在 → GetOldVersion 返回 "ascendcl_impl"
TEST_F(AclRuntimeConfigTest, GetOldVersion_when_acl_rt_impl_not_found) {
    MOCKER(&GetEnv).stubs().will(returnValue(string("/tmp/ut_aclruntime_nonexist")));
    ASSERT_EQ(AclRuntimeConfig::GetOldVersion(), "ascendcl_impl");
}

// 缓存验证：第二次调用不应再读环境变量
TEST_F(AclRuntimeConfigTest, cached_value_returned_on_second_call) {
    MOCKER(&GetEnv).stubs().will(returnValue(string("/tmp/ut_aclruntime_nonexist")));
    ASSERT_EQ(AclRuntimeConfig::GetOldVersion(), "ascendcl_impl");
    ASSERT_EQ(AclRuntimeConfig::GetOldVersion(), "ascendcl_impl");
}

// ---------- HasSymbolInSo ----------

// HasSymbolInSo: 目标符号存在于 .dynsym 中 → 返回 true
TEST_F(AclRuntimeConfigTest, has_symbol_in_so_when_symbol_exists) {
    FileGuard guard("/tmp/test_has_symbol.so");
    ASSERT_TRUE(CreateTestElfSo(guard.path, "aclrtSetDeviceImpl"));
    EXPECT_TRUE(AclRuntimeConfig::HasSymbolInSo(guard.path, "aclrtSetDeviceImpl"));
}

// HasSymbolInSo: 目标符号不在 .dynsym 中 → 返回 false
TEST_F(AclRuntimeConfigTest, has_symbol_in_so_when_symbol_not_exists) {
    FileGuard guard("/tmp/test_no_symbol.so");
    ASSERT_TRUE(CreateTestElfSo(guard.path, "otherSymbol"));
    EXPECT_FALSE(AclRuntimeConfig::HasSymbolInSo(guard.path, "aclrtSetDeviceImpl"));
}

// HasSymbolInSo: 文件不存在 → 返回 false
TEST_F(AclRuntimeConfigTest, has_symbol_in_so_when_file_not_exists) {
    EXPECT_FALSE(AclRuntimeConfig::HasSymbolInSo("/tmp/nonexistent_so_file.so", "anySymbol"));
}

// ---------- GetSoByVersion (新分支) ----------

// libruntime.so 含 aclrtSetDeviceImpl 且非仿真 → 返回 "runtime"
TEST_F(AclRuntimeConfigTest, GetSoByVersion_returns_runtime_when_symbol_found) {
    TempDirGuard dir("/tmp/ut_aclruntime");
    ASSERT_TRUE(dir.ok_);
    string lib64 = dir.path + "/lib64";
    ASSERT_EQ(::mkdir(lib64.c_str(), 0755), 0);
    string runtimeSo = lib64 + "/libruntime.so";
    FileGuard soGuard(runtimeSo);
    ASSERT_TRUE(CreateTestElfSo(runtimeSo, "aclrtSetDeviceImpl"));
    ::unsetenv("IS_SIMULATOR_ENV");
    MOCKER(&GetEnv).stubs().will(returnValue(dir.path));
    ASSERT_EQ(AclRuntimeLibName(), "runtime");
}

// libruntime.so 含 aclrtSetDeviceImpl 且仿真 → 返回 "runtime_camodel"
TEST_F(AclRuntimeConfigTest, GetSoByVersion_returns_runtime_camodel_when_simulator) {
    TempDirGuard dir("/tmp/ut_aclruntime");
    ASSERT_TRUE(dir.ok_);
    string lib64 = dir.path + "/lib64";
    ASSERT_EQ(::mkdir(lib64.c_str(), 0755), 0);
    string runtimeSo = lib64 + "/libruntime.so";
    FileGuard soGuard(runtimeSo);
    ASSERT_TRUE(CreateTestElfSo(runtimeSo, "aclrtSetDeviceImpl"));
    MOCKER(&GetEnv).stubs().will(returnValue(dir.path)).then(returnValue(string("true")));
    ASSERT_EQ(AclRuntimeLibName(), "runtime_camodel");
}

// libruntime.so 存在但不含 aclrtSetDeviceImpl → 回退 GetOldVersion
TEST_F(AclRuntimeConfigTest, GetSoByVersion_fallback_when_symbol_not_found) {
    TempDirGuard dir("/tmp/ut_aclruntime");
    ASSERT_TRUE(dir.ok_);
    string lib64 = dir.path + "/lib64";
    ASSERT_EQ(::mkdir(lib64.c_str(), 0755), 0);
    string runtimeSo = lib64 + "/libruntime.so";
    FileGuard soGuard(runtimeSo);
    ASSERT_TRUE(CreateTestElfSo(runtimeSo, "otherSymbol"));
    MOCKER(&GetEnv).stubs().will(returnValue(dir.path));
    ASSERT_EQ(AclRuntimeLibName(), "ascendcl_impl");
}

// libruntime.so 不存在 → 回退 GetOldVersion
TEST_F(AclRuntimeConfigTest, GetSoByVersion_fallback_when_libruntime_not_found) {
    MOCKER(&GetEnv).stubs().will(returnValue(string("/tmp/ut_aclruntime_nonexist")));
    ASSERT_EQ(AclRuntimeLibName(), "ascendcl_impl");
}
