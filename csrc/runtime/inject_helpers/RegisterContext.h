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

#ifndef __REGISTER_CONTEXT_H__
#define __REGISTER_CONTEXT_H__

#pragma once

#include <elf.h>
#include <map>
#include <string>
#include <vector>
#include <memory>

#include "acl.h"
#include "runtime.h"
#include "KernelMetaDataParser.h"

constexpr uint32_t FLAG_A2_AIV = 0x930000;
constexpr uint32_t FLAG_A2_AIC = 0x940000;
constexpr uint32_t FLAG_DEFAULT_VALUE = 0x810000;

bool GetSymInfoFromBinary(const char *data, uint64_t length, std::vector<std::string> &kernelNames,
                          std::vector<uint64_t> &kernelOffsets);
bool HasStaticStub(const rtDevBinary_t &binary);
bool HasDebugLine(const rtDevBinary_t &binary);

/**
 * @brief 查询二进制的段信息
 * @param binary 要查询的算子二进制信息
 * @param [out] headers 查询得到的段信息，返回值为 true 时有效
 * @return true  查询成功
 *         false 查询失败，二进制信息为空或二进制解析成 ELF 失败
 */
bool GetSectionHeaders(rtDevBinary_t const &binary, std::map<std::string, Elf64_Shdr> &headers);

/**
 * @brief 查询二进制的段信息
 * @param binary 要查询的算子二进制数据
 * @param [out] headers 查询得到的段信息，返回值为 true 时有效
 * @return true  查询成功
 *         false 查询失败，二进制信息为空或二进制解析成 ELF 失败
 */
bool GetSectionHeaders(std::vector<char> const &binary, std::map<std::string, Elf64_Shdr> &headers);

uint32_t GetFlagByMagic(const uint32_t magic);

uint64_t GetMetaSection(const rtDevBinary_t &binary, const std::string &kernelName, std::vector<uint8_t> &metaData);

bool GetSimtSymbolFromBinary(const char *data, uint64_t length);

KernelType MagicToKernelType(uint32_t magic);

struct RegisterParam {
    uint64_t regId{0};
    uint32_t magic{0};
    // 注意这个结构体要拷贝的话，里面指针的数据都要拷贝一份
    aclrtBinaryLoadOptions options{};
    std::vector<aclrtBinaryLoadOption> optionData{};
    bool needUnload{false};
    bool optionsIsNull{false};

    RegisterParam() = default;

    RegisterParam(uint64_t regId, uint32_t magic, const aclrtBinaryLoadOptions *op, bool needUnload, bool optionsIsNull);

    RegisterParam(const RegisterParam &param) { *this = param; }

    RegisterParam& operator=(const RegisterParam &param)
    {
        if (this != &param) {
            regId = param.regId;
            magic = param.magic;
            options = param.options;
            optionData = param.optionData;
            needUnload = param.needUnload;
            options.options = optionData.data();
            optionsIsNull = param.optionsIsNull;
        }
        return *this;
    }
};

/**
 * 管理单个Kernel二进制信息.
 * 当前包含Kernel二进制所有kernelName, kernelOffset信息
 * 可以Dump二进制到磁盘.
 * 提供对于FuncContext的接口来查寻对应tilingKey的KernelName以及KernelOffset
 */
class RegisterContext {
public:
    ~RegisterContext();

    bool Init(const char *binPath, aclrtBinHandle binHandle, const RegisterParam &param);

    bool Init(const std::vector<char> &elfData, aclrtBinHandle binHandle, const RegisterParam &param);

    bool Save(const std::string &outputPath) const;

    bool HasStaticStub() const { return hasStaticStub_; };

    bool HasDebugLine() const { return hasDebugLine_; };

    uint64_t GetKernelOffsetByName(const std::string &kernelName) const;

    std::string GetKernelName(uint64_t tilingKey) const;

    void *GetHandle() const { return hdl_; }

    std::vector<char> const &GetElfData(void) const { return elfData_; }

    uint32_t GetMagic() const { return param_.magic; }

    aclrtBinaryLoadOptionType GetBinaryType() const { return type_; }

    uint64_t GetRegisterId() const { return param_.regId; };

    std::shared_ptr<RegisterContext> Clone(const std::string &newBinPath) const;

    bool ParseMetaData(const std::string &kernelName, OpMemInfo &memInfo) const;

    /// 获取kernelName对应的symbol名称判断是否为mix算子，提供给动态插桩使用
    bool KernelSymbolNameIsMix(const std::string &kernelName) const;

    bool HasSimtSymbol() const;

private:
    std::string binPath_;
    std::vector<char> elfData_;
    std::vector<uint64_t> kernelOffsets_;
    std::vector<std::string> kernelSymbolNames_;
    RegisterParam param_{};
    uint32_t version_{0};
    bool hasStaticStub_{false};
    bool hasDebugLine_{false};
    // bin handle, 原型为aclrtBinHandle
    void *hdl_{nullptr};
    bool inited_{false};
    bool isFromFile_ {false};
    bool hasSimt_{false};
    aclrtBinaryLoadOptionType type_ {aclrtBinaryLoadOptionType::NONE};

    std::shared_ptr<RegisterContext> CloneFromBin(const std::string &newBinPath) const;
};

using RegisterContextSP = std::shared_ptr<RegisterContext>;

#endif // __REGISTER_CONTEXT_H__