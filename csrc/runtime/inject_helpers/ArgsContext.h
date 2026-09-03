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

#ifndef __ARGS_CONTEXT_H__
#define __ARGS_CONTEXT_H__

#pragma once

#include <map>
#include <string>
#include <vector>
#include <memory>

#include "acl.h"
#include "runtime.h"
#include "nlohmann/json.hpp"
#include "KernelMetaDataParser.h"

class ArgsContext;

using ArgsContextSP = std::shared_ptr<ArgsContext>;

// save with json format
struct DumperContext {
    std::string binPath;
    uint32_t blockDim = 0;
    int visibleDevId = 0;
    int devId = 0;
    bool isFFTS = false;
    std::vector<std::string> inputPathList;
    std::vector<uint64_t> inputSizeList;
    std::string kernelName;
    uint32_t magic = 0;
    std::string tilingDataPath;
    uint32_t tilingDataSize = 0;
    uint64_t tilingKey = 0;
    bool hasTilingKey = false;
    bool isAclNew = false;

    bool ToJson(nlohmann::json &jsonData) const;
};

// launch接口和args相关的参数，用于解析args中的地址信息
struct AclrtLaunchArgsInfo {
    void *hostArgs{};
    size_t argsSize{};
    aclrtPlaceHolderInfo *placeHolderArray{};
    size_t placeHolderNum{};
    // ArgsArray 路径无 placeholder，当 meta 解析出 tiling 的 paramsNo 时，
    // 由调用方据此推算 tiling 指针在 hostArgs 中的字节偏移并填入此处；
    // 0 表示未知，按无 tiling 处理。
    uint32_t tilingAddrOffset{};
};

/*
 * 保存kernel args的信息的基类
 * 子类需要实现落盘功能以及插入一个指针参数
 */
class ArgsContext {
public:
    ArgsContext() = default;
    virtual ~ArgsContext() = default;
    // 插入GM指针
    virtual bool ExpandArgs(void *param, size_t paramSize, uint32_t &paramOffset) = 0;
    // 落盘上下文信息, 使得kernel-launcher加载后使用
    virtual bool Save(const std::string &outputPath, DumperContext &config, OpMemInfo &memInfo, bool isSink) = 0;
    // 为了能反复使用ExpandArgs函数，需要实现拷贝
    virtual ArgsContextSP Clone(void) const = 0;

    virtual uint32_t GetLastParamOffset() { return 0; }

    virtual bool GetTilingData(std::vector<uint8_t> &data) const { return false; }
protected:
    // 多态基类中的拷贝构造函数、拷贝赋值操作符、移动构造函数、移动赋值操作符必须为非public函数
    ArgsContext(const ArgsContext &) = default;

    ArgsContext(ArgsContext &&) = default;

    ArgsContext& operator=(const ArgsContext &) = default;

    ArgsContext& operator=(ArgsContext &&) = default;
};

bool DumpInputData(const std::string &outputDir,
                   const std::vector<AddrInfo> &inputParamsAddrInfos,
                   DumperContext &config);

bool DumpTilingData(const std::string &outputDir,
                    std::vector<uint8_t> const& tilingData,
                    DumperContext &config);

#endif // __ARGS_CONTEXT_H__
