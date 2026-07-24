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

#include "HijackedFunc.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "runtime/inject_helpers/RegisterManager.h"
#include "runtime/inject_helpers/ProfDataCollect.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "utils/InjectLogger.h"

HijackedFuncOfAclrtBinaryLoadFromDataImpl::HijackedFuncOfAclrtBinaryLoadFromDataImpl()
    : HijackedFuncType(AclRuntimeLibName(), "aclrtBinaryLoadFromDataImpl") {}

void HijackedFuncOfAclrtBinaryLoadFromDataImpl::Pre(const void *data, size_t length,
                                                    const aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle)
{
    bin_ = data;
    binHandle_ = binHandle;
    options_ = const_cast<aclrtBinaryLoadOptions *>(options); // NOLINT(cppcoreguidelines-pro-type-const-cast)
    length_ = length;
}

aclError HijackedFuncOfAclrtBinaryLoadFromDataImpl::Post(aclError ret)
{
    if (ret != ACL_SUCCESS || bin_ == nullptr || binHandle_ == nullptr) {
        return ret;
    }
    uint64_t magic = RT_DEV_BINARY_MAGIC_ELF;
    if (options_) {
        for (size_t i = 0; i < options_->numOpt; i++) {
            // AclrtBinaryLoadFromData 接口的 magic 应该从 options 中解析
            if (options_->options[i].type == ACL_RT_BINARY_LOAD_OPT_MAGIC ||
                options_->options[i].type == ACL_RT_BINARY_LOAD_OPT_LAZY_MAGIC) {
                magic = options_->options[i].value.magic;
            }
            if ((options_->options + i)->type == ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE) {
                magic = RT_DEV_BINARY_MAGIC_ELF_AICPU;
                break;
            }
        }
    }
    auto ctx = RegisterManager::Instance().CreateContext(bin_, length_, *binHandle_, magic, options_);
    if (!ctx) {
        DEBUG_LOG("aclrtBinaryLoadFromDataImpl create context failed");
        return ret;
    }
    if (IsOpProf() && ProfConfig::Instance().IsSimulator()) {
        ProfDataCollect::SaveObject(ctx);
    }
    return ret;
}
