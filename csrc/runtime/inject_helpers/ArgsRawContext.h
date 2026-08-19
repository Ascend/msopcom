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

#include <map>
#include <string>
#include <vector>
#include <memory>

#include "acl.h"
#include "runtime.h"
#include "ArgsContext.h"

/*
 * 管理rtKernelLaunch时的入参的args信息
 */
class ArgsRawContext : public ArgsContext {
public:
    ArgsRawContext(void *args, uint32_t argsSize, bool isDeviceArgs = false)
        : ArgsContext(), args_(args), argsSize_(argsSize), isDeviceArgs_(isDeviceArgs),  placeHolderArray_() {}

    ArgsRawContext(void *args, uint32_t argsSize, const std::vector<aclrtPlaceHolderInfo> &placeHolderArray)
        : ArgsContext(), args_(args), argsSize_(argsSize), isDeviceArgs_(false), placeHolderArray_(placeHolderArray)
    {
        // 图模式(sink)下 dump 回调异步执行，调用方的 hostArgs 缓冲区可能在回调前被复用/释放，
        // 这里提前拷贝一份，保证 dump 时能读到稳定的入参。
        if (args != nullptr && argsSize > 0) {
            argsCopy_.assign(static_cast<uint8_t *>(args), static_cast<uint8_t *>(args) + argsSize);
            args_ = argsCopy_.data();
        }
    }

    ~ArgsRawContext() override;

    bool ExpandArgs(void *param, size_t paramSize, uint32_t &paramOffset) override;

    uint32_t GetLastParamOffset() override;

    bool Save(const std::string &outputPath, DumperContext &config, OpMemInfo &memInfo, bool isSink) override;

    ArgsContextSP Clone(void) const override;

    void *const GetArgs() const { return args_; };

    uint32_t GetArgsSize() const { return argsSize_; }

    const std::vector<aclrtPlaceHolderInfo> &GetPlaceholderInfo() const { return placeHolderArray_;}

    void FreeArgs();

    bool GetTilingData(std::vector<uint8_t> &data) const override;
private:
    void *args_;
    uint32_t argsSize_;
    bool isDeviceArgs_;
    bool needFree_ = false;
    std::vector<uint8_t> argsWithMemInfo_;
    std::vector<uint8_t> argsCopy_;
    std::vector<aclrtPlaceHolderInfo> placeHolderArray_;
};
