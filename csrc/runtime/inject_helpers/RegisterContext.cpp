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

#include "RegisterContext.h"

#include <algorithm>
#include <map>
#include <thread>
#include <string>
#include <sstream>
#include <iostream>
#include <iterator>
#include <cstring>

#include "DeviceContext.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "ascend_dump.h"
#include "utils/ElfLoader.h"
#include "utils/FileSystem.h"
#include "utils/InjectLogger.h"
#include "utils/TypeTraits.h"
#include "utils/Ustring.h"

using namespace std;

namespace {
inline bool IsA2Version(const string &socVersion)
{
    return socVersion.find("Ascend910B") != std::string::npos;
}

constexpr char const *MIX_AIC_TAIL = "_mix_aic";
constexpr char const *MIX_AIV_TAIL = "_mix_aiv";

bool EndsWithMixAivOrAic(const std::string &kernelName)
{
    if (kernelName.empty()) {
        return false;
    }

    const size_t suffixLen = strlen(MIX_AIC_TAIL);
    if (kernelName.length() < suffixLen) {
        return false;
    }

    std::string endPart = kernelName.substr(kernelName.length() - suffixLen);
    return (endPart == MIX_AIC_TAIL) || (endPart == MIX_AIV_TAIL);
}

// 直接解析 ELF .symtab 得到的内核符号信息
struct ElfKernelSymbols {
    std::vector<std::string> names; // .text 中的 global FUNC 符号名
    std::vector<uint64_t> offsets; // 与 names 一一对应的 st_value
    bool hasSimt{false}; // 是否存在 .text 中的局部 $t.* SIMT 符号
};

// 遍历符号表，收集 .text 中的 global FUNC 符号与局部 $t.* SIMT 标记
bool CollectKernelSymbols(const char *data, uint64_t length, const Elf64_Shdr &symtab, const Elf64_Shdr &strtab,
    std::size_t textIndex, bool foundText, ElfKernelSymbols &out) {
    const char *strings = data + strtab.sh_offset;
    const uint64_t symCount = symtab.sh_size / sizeof(Elf64_Sym);
    for (uint64_t i = 0; i < symCount; ++i) {
        Elf64_Sym sym{};
        if (!elf::ReadValue<Elf64_Sym>(data, length, symtab.sh_offset + i * sizeof(Elf64_Sym), sym)) {
            return false;
        }
        if (sym.st_name >= strtab.sh_size || !foundText || sym.st_shndx != textIndex) {
            continue;
        }
        const char *name = strings + sym.st_name;
        const std::size_t nameBytes = static_cast<std::size_t>(strtab.sh_size - sym.st_name);
        const unsigned char bind = ELF64_ST_BIND(sym.st_info);
        if (bind == STB_GLOBAL && ELF64_ST_TYPE(sym.st_info) == STT_FUNC) {
            out.offsets.push_back(sym.st_value);
            const std::size_t nameLen = static_cast<std::size_t>(std::find(name, name + nameBytes, '\0') - name);
            out.names.emplace_back(name, nameLen);
        } else if (bind == STB_LOCAL && nameBytes >= 2 && name[0] == '$' && name[1] == 't') {
            out.hasSimt = true;
        }
    }
    return true;
}

// 用进程内 ELF 解析替代 `llvm-objdump -t` 文本导出。
// 静态插桩后的内核可达 GB 级（.symtab 数百 MB），此前实现把整个二进制经 stdin 喂给 objdump，
// 再物化全部符号文本、fork grep、逐行 istringstream 分词，且同一份二进制解析两次，开销极大。
// 这里直接遍历 SHT_SYMTAB，一次扫描即可同时得到 kernel 符号与 SIMT 标记。
bool ScanElfKernelSymbols(const char *data, uint64_t length, ElfKernelSymbols &out) {
    Elf64_Shdr symtab{};
    Elf64_Shdr strtab{};
    std::size_t textIndex = 0;
    bool foundText = false;
    if (!ElfLoader::LocateKernelSymtab(data, length, symtab, strtab, textIndex, foundText)) {
        return false;
    }
    return CollectKernelSymbols(data, length, symtab, strtab, textIndex, foundText, out);
}
}

RegisterParam::RegisterParam(uint64_t regId, uint32_t magic,
    const aclrtBinaryLoadOptions *op, bool needUnload, bool optionsIsNull) : regId(regId), magic(magic),
    needUnload(needUnload), optionsIsNull(optionsIsNull)
{
    optionData.clear();
    if (op == nullptr) {
        return;
    }
    if (op->numOpt == 0) {
        return;
    }
    options.numOpt = op->numOpt;
    if (op->options == nullptr) {
        return;
    }
    // check numOpt
    for (size_t i = 0; i < op->numOpt; i++) {
        aclrtBinaryLoadOption copyLoadOption = *(op->options + i);
        optionData.push_back(copyLoadOption);
    }
    options.options = optionData.data();
}

bool ScanKernelSymbols(
    const char *data, uint64_t length, vector<string> &kernelNames, vector<uint64_t> &kernelOffsets, bool &hasSimt) {
    if (data == nullptr) {
        return false;
    }

    ElfKernelSymbols symbols;
    if (!ScanElfKernelSymbols(data, length, symbols)) {
        return false;
    }
    kernelNames = std::move(symbols.names);
    kernelOffsets = std::move(symbols.offsets);
    hasSimt = symbols.hasSimt;
    return true;
}

bool GetSymInfoFromBinary(
    const char *data, uint64_t length, vector<string> &kernelNames, vector<uint64_t> &kernelOffsets) {
    bool hasSimt = false;
    return ScanKernelSymbols(data, length, kernelNames, kernelOffsets, hasSimt);
}

bool GetSectionHeaders(rtDevBinary_t const &binary, map<string, Elf64_Shdr> &headers)
{
    if (binary.data == nullptr || binary.length == 0) {
        ERROR_LOG("empty dev binary info");
        return false;
    }

    return ElfLoader::ScanSectionHeaders(static_cast<char const *>(binary.data), binary.length, headers);
}

bool GetSectionHeaders(std::vector<char> const &binary, std::map<std::string, Elf64_Shdr> &headers)
{
    return ElfLoader::ScanSectionHeaders(binary.data(), binary.size(), headers);
}

bool HasStaticStub(const rtDevBinary_t &binary)
{
    map<string, Elf64_Shdr> headers;
    if (!GetSectionHeaders(binary, headers)) {
        return false;
    }
    static string sectionName {".init_array_sanitizer_file_mapping"};
    return as_const(headers).find(sectionName) != headers.cend();
}

bool HasDebugLine(const rtDevBinary_t &binary)
{
    map<string, Elf64_Shdr> headers;
    if (!GetSectionHeaders(binary, headers)) {
        return false;
    }
    return as_const(headers).find(".debug_line") != headers.cend();
}

uint32_t GetFlagByMagic(const uint32_t magic)
{
    std::string socVersion = DeviceContext::Local().GetSocVersion();
    // only fix for A2
    if (IsA2Version(socVersion)) {
        switch (magic) {
            case RT_DEV_BINARY_MAGIC_ELF_AIVEC:
                return FLAG_A2_AIV;
            case RT_DEV_BINARY_MAGIC_ELF_AICUBE:
            case RT_DEV_BINARY_MAGIC_ELF:
                return FLAG_A2_AIC;
            default:
                return 0;
        }
    }
    return 0;
}

KernelType MagicToKernelType(uint32_t magic)
{
    KernelType kernelType {KernelType::INVALID};
    if (magic == RT_DEV_BINARY_MAGIC_ELF_AIVEC) {
        kernelType = KernelType::AIVEC;
    } else if (magic == RT_DEV_BINARY_MAGIC_ELF_AICUBE) {
        kernelType = KernelType::AICUBE;
    } else if (magic == RT_DEV_BINARY_MAGIC_ELF) {
        kernelType = KernelType::MIX;
    } else if (magic == RT_DEV_BINARY_MAGIC_ELF_AICPU) {
        kernelType = KernelType::AICPU;
    } else {
        DEBUG_LOG("INVALID kernel binary magic number %u", magic);
    }
    return kernelType;
}

uint64_t GetMetaSection(const rtDevBinary_t &binary, const string &kernelName, vector<uint8_t> &metaData)
{
    if (binary.data == nullptr || binary.length == 0) {
        WARN_LOG("Get meta section failed: binary data is null or empty");
        return 0;
    }
    const char *binData = static_cast<const char *>(binary.data);
    // 原地解析段表，避免此前 vector<char> buffer(...) + ElfLoader::FromBuffer 对整块二进制的两次全量拷贝
    std::map<std::string, Elf64_Shdr> headers;
    std::multimap<std::string, Elf64_Shdr> allSections;
    if (!ElfLoader::ScanSectionHeaders(binData, binary.length, headers, &allSections)) {
        WARN_LOG("Get meta section failed: unable to parse section headers from binary");
        return 0;
    }
    std::string sectionName = ".ascend.meta." + kernelName;
    bool found = headers.find(sectionName) != headers.end();
    if (!found) {
        // MIX 二进制的 meta 段名带有 _mix_aic/_mix_aiv 尾缀，与 kernelName（.text 符号）不一致，
        // 需要额外尝试尾缀变体，避免精确匹配失败。
        for (const auto &tail : {MIX_AIC_TAIL, MIX_AIV_TAIL}) {
            const std::string candidate = sectionName + tail;
            if (headers.find(candidate) != headers.end()) {
                sectionName = candidate;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        DEBUG_LOG("Cannot find meta data in binary file");
        return 0;
    }
    DEBUG_LOG("Get meta data from section %s", sectionName.c_str());
    // 只拷贝目标 meta 段（可能同名多个，按段表顺序拼接）本身的字节
    if (!elf::ReadAllRawData(binData, binary.length, allSections, sectionName, metaData)) {
        WARN_LOG("Get meta section failed: invalid range for section %s", sectionName.c_str());
        return 0;
    }
    return metaData.size();
}

bool GetSimtSymbolFromBinary(const char *data, uint64_t length)
{
    if (data == nullptr) {
        return false;
    }
    ElfKernelSymbols symbols;
    if (!ScanElfKernelSymbols(data, length, symbols)) {
        return false;
    }
    return symbols.hasSimt;
}

RegisterContext::~RegisterContext()
{
    if (param_.needUnload) {
        aclrtBinaryUnLoadImplOrigin(hdl_);
    }
}

bool RegisterContext::Init(vector<char> elfData, aclrtBinHandle binHandle, const RegisterParam &param) {
    // 值传递 + move，避免对 GB 级内核二进制再做一次 elfData_ = elfData 的全量拷贝
    elfData_ = std::move(elfData);
    param_ = param;
    // 一次遍历符号表同时得到 kernelNames/kernelOffsets/hasSimt（此前调用两次各扫描一遍 3000 万符号）
    bool hasSimt = false;
    if (!ScanKernelSymbols(elfData_.data(), elfData_.size(), kernelSymbolNames_, kernelOffsets_, hasSimt)) {
        DEBUG_LOG("Get symbol info from binary failed.");
        return false;
    }
    DEBUG_LOG("Get total %lu kernelNames", kernelSymbolNames_.size());
    hasSimt_ = hasSimt;
    // 一次解析节头表同时判定 static stub / debug_line（此前 HasStaticStub/HasDebugLine 各扫描一次）
    std::map<std::string, Elf64_Shdr> sectionHeaders;
    if (ElfLoader::ScanSectionHeaders(elfData_.data(), elfData_.size(), sectionHeaders)) {
        hasStaticStub_ = sectionHeaders.find(".init_array_sanitizer_file_mapping") != sectionHeaders.cend();
        hasDebugLine_ = sectionHeaders.find(".debug_line") != sectionHeaders.cend();
    }
    hdl_ = binHandle;
    return true;
}

bool RegisterContext::Init(const char *binPath, aclrtBinHandle binHandle, const RegisterParam &param)
{
    vector<char> elfData;
    if (ReadBinary(binPath, elfData) == 0U) {
        return false;
    }
    if (param.options.options != nullptr) {
        type_ = param.options.options->type;
    }
    binPath_ = binPath;
    isFromFile_ = true;
    return Init(std::move(elfData), binHandle, param);
}

bool RegisterContext::Save(const string &outputPath) const
{
    Elf64_Ehdr header{};
    if (!ElfLoader::LoadHeader(elfData_, header)) {
        ERROR_LOG("Read elf header failed when load elf from buffer");
        return false;
    }
    // 编译算子时没有指定target， flag值会为这个
    if (header.e_flags == FLAG_DEFAULT_VALUE) {
        auto correctFlag = GetFlagByMagic(param_.magic);
        if (correctFlag > 0) {
            header.e_flags = correctFlag;
            const char *base = static_cast<const char*>(static_cast<const void *>(&header));
            // 仅在需要改写头部的分支做一次拷贝，常规路径直接写 elfData_，避免 GB 级全量拷贝
            auto elfData = elfData_;
            std::copy(base, base + sizeof(Elf64_Ehdr), elfData.data());
            DEBUG_LOG("default flag of op is forced set to %u", correctFlag);
            size_t written = WriteBinary(outputPath, elfData.data(), elfData.size());
            return written == elfData.size();
        }
    }

    size_t written = WriteBinary(outputPath, elfData_.data(), elfData_.size());
    return written == elfData_.size();
}

uint64_t RegisterContext::GetKernelOffsetByName(const std::string &kernelName) const
{
    uint64_t kernelOffset = UINT64_MAX;
    for (size_t i = 0; i < kernelSymbolNames_.size(); i++) {
        if (kernelName == kernelSymbolNames_[i]
            || kernelName + MIX_AIC_TAIL == kernelSymbolNames_[i]
            || kernelName + MIX_AIV_TAIL == kernelSymbolNames_[i]) {
            kernelOffset = kernelOffsets_[i];
            break;
        }
    }
    if (kernelOffset == UINT64_MAX) {
        DEBUG_LOG("Can not find kernel name %s in binary kernel names.", kernelName.c_str());
        kernelOffset = 0;
    }
    return kernelOffset;
}

string RegisterContext::GetKernelName(uint64_t tilingKey) const
{
    string nameSuffix = "_" + to_string(tilingKey);
    vector<string> suffixNames = {nameSuffix, nameSuffix + MIX_AIC_TAIL, nameSuffix + MIX_AIV_TAIL};

    for (const std::string &name : kernelSymbolNames_) {
        for (const std::string &suffix: suffixNames) {
            if (EndsWith(name, suffix)) {
                return name;
            }
        }
    }
    DEBUG_LOG("Get kernel name with tiling key=%lu failed.", tilingKey);
    return "";
}

std::shared_ptr<RegisterContext> RegisterContext::CloneFromBin(const std::string &newBinPath) const
{
    vector<char> data;
    auto readSize = ReadBinary(newBinPath, data);
    if (readSize == 0) {
        return nullptr;
    }
    aclrtBinHandle binHandle;
    aclError ret{};
    if (param_.optionsIsNull) {
        ret = aclrtBinaryLoadFromDataImplOrigin(static_cast<const void*>(data.data()),
            data.size(), nullptr, &binHandle);
    } else {
        ret = aclrtBinaryLoadFromDataImplOrigin(static_cast<const void*>(data.data()),
            data.size(), &param_.options, &binHandle);
    }
    if (ret != ACL_SUCCESS) {
        DEBUG_LOG("BinaryLoadFromData failed when clone, ret=%d", ret);
        return nullptr;
    }
    auto newRegCtx = make_shared<RegisterContext>();
    RegisterParam newParam(param_.regId, param_.magic, &param_.options, true, param_.optionsIsNull);
    if (!newRegCtx->Init(std::move(data), binHandle, newParam)) {
        DEBUG_LOG("Init register context from data failed when clone");
        return nullptr;
    }
    return newRegCtx;
}

// 派生类的RegisterContext有自己的注册方法
RegisterContextSP RegisterContext::Clone(const std::string &newBinPath) const
{
    // move to in future
    if (!isFromFile_) {
        return CloneFromBin(newBinPath);
    }
    string curJsonPath = binPath_.substr(0, binPath_.length() - 1) + "json";
    // dump json with magic info
    string targetJsonPath = newBinPath.substr(0, newBinPath.length() - 1) + "json";
    if (IsExist(curJsonPath) && !CopyFile(curJsonPath, targetJsonPath)) {
        return nullptr;
    }
    aclrtBinHandle binHandle;
    // Fromdata to fromfile, param_.options must be nullptr
    auto options = param_.options;
    aclError ret{};
    if (param_.optionsIsNull) {
        ret = aclrtBinaryLoadFromFileImplOrigin(newBinPath.c_str(), nullptr, &binHandle);
    } else {
        ret = aclrtBinaryLoadFromFileImplOrigin(newBinPath.c_str(), &options, &binHandle);
    }
    if (ret != ACL_SUCCESS) {
        DEBUG_LOG("BinaryLoadFromFile failed when clone, ret=%d", ret);
        return nullptr;
    }
    auto newRegCtx = make_shared<RegisterContext>();
    RegisterParam newParam{param_.regId, param_.magic, &param_.options, true, param_.optionsIsNull};
    if (!newRegCtx->Init(newBinPath.c_str(), binHandle, newParam)) {
        DEBUG_LOG("Init register context failed when clone");
        return nullptr;
    }
    return newRegCtx;
}

bool RegisterContext::ParseMetaData(const string &kernelName, OpMemInfo &memInfo) const
{
    vector<uint8_t> metaData;
    rtDevBinary_t tempDevBin{};
    tempDevBin.data = elfData_.data();
    tempDevBin.length = elfData_.size();
    auto shSize = GetMetaSection(tempDevBin, kernelName, metaData);
    if (shSize == 0) {
        DEBUG_LOG("Get meta data failed");
        return false;
    }
    auto parser = KernelMetaDataParser(metaData);
    if (parser.Parse()) {
        memInfo = parser.GetOpMemInfo();
        return true;
    }
    return false;
}

bool RegisterContext::KernelSymbolNameIsMix(const std::string &kernelName) const
{
    if (EndsWithMixAivOrAic(kernelName)) {
        return true;
    }
    for (size_t i = 0; i < kernelSymbolNames_.size(); i++) {
        auto kernelSymbolName = kernelSymbolNames_[i];
        if (EndsWithMixAivOrAic(kernelSymbolName)) {
            auto kernelSymbolNamePrefix = kernelSymbolName.substr(0, kernelSymbolName.length() - strlen(MIX_AIC_TAIL));
            if (kernelSymbolNamePrefix == kernelName) {
                return true;
            }
        }
    }
    return false;
}

bool RegisterContext::HasSimtSymbol() const { return hasSimt_; }
