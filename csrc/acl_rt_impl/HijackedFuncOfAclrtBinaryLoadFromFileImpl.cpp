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


#include <elf.h>
#include <fstream>
#include <cstring>

#include "HijackedFunc.h"
#include "utils/FileSystem.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "runtime/inject_helpers/RegisterManager.h"
#include "utils/InjectLogger.h"

HijackedFuncOfAclrtBinaryLoadFromFileImpl::HijackedFuncOfAclrtBinaryLoadFromFileImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtBinaryLoadFromFileImpl") {}

void HijackedFuncOfAclrtBinaryLoadFromFileImpl::Pre(
    const char* binPath, aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle)
{
    binPath_ = binPath;
    binHandle_ = binHandle;
    options_ = options;
}

aclError HijackedFuncOfAclrtBinaryLoadFromFileImpl::Post(aclError ret)
{
    if (ret != ACL_SUCCESS || binPath_ == nullptr || binHandle_ == nullptr) {
        return ret;
    }

    uint32_t magic = RT_DEV_BINARY_MAGIC_ELF;
    std::string magicStr;
    std::string jsonPath(binPath_);

    size_t lastDot = jsonPath.find_last_of('.');
    std::string suffix{};
    if (lastDot != std::string::npos) {
        suffix = jsonPath.substr(lastDot);
    }

    if (suffix == ".o") {
        jsonPath = jsonPath.substr(0, lastDot) + ".json";
        Elf64_Ehdr header{};
        std::ifstream binary(binPath_, std::ios::binary);
        bool nativeElf = binary.read(reinterpret_cast<char *>(&header), sizeof(header)) &&
            std::memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 &&
            header.e_flags != 0 && header.e_flags != FLAG_DEFAULT_VALUE;
        // 原生ELF头已携带目标核信息（但没有同名 JSON）的场景下，BinaryLoadFromFile可以不要求JSON；仍按通用ELF注册以保留插桩
        // 旧格式依赖 JSON，或者存在 JSON 的场景还是保留原来的解析流程
        if (nativeElf && Realpath(jsonPath).empty()) {
            DEBUG_LOG("Use native ELF metadata without optional kernel JSON. path:%.2048s", binPath_);
        } else if (!ReadMagicFromKernelJson(jsonPath, magicStr) || !ParseMagicStr(magicStr, magic)) {
            WARN_LOG("Parse magic from kernel JSON failed.");
        }
    } else if (suffix == ".json") {
        DEBUG_LOG("The kernel file path : %.2048s is invalid", binPath_);
        return ret;
    } else {
        DEBUG_LOG("The kernel file path : %.2048s is invalid", binPath_);
    }

    auto ctx = RegisterManager::Instance().CreateContext(binPath_, *binHandle_, magic, options_);
    if (!ctx) {
        return ret;
    }
    if (IsOpProf() && ProfConfig::Instance().IsSimulator()) {
        ProfDataCollect::SaveObject(ctx);
    }
    return ret;
}
