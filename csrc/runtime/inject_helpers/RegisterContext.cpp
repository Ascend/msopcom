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
#include "utils/PipeCall.h"
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

}

inline bool GetObjdumpOutput(std::string const &binaryData, std::string &output)
{
    std::string objdumpOutput;
    bool ret = PipeCall({"llvm-objdump", "-t", "-"}, objdumpOutput, binaryData);
    if (!ret) {
        DEBUG_LOG("Call llvm-objdump failed");
        return false;
    }

    ret = PipeCall({"grep", "g     F .text"}, output, objdumpOutput);
    if (!ret) {
        DEBUG_LOG("Grep global function symbols from objdump output failed. Fallback to original objdump output");
        output = std::move(objdumpOutput);
    }

    return true;
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

bool GetSymInfoFromBinary(const char *data, uint64_t length, vector<string> &kernelNames,
                          vector<uint64_t> &kernelOffsets)
{
    if (data == nullptr) {
        return false;
    }

    std::string binaryData(data, length);
    std::string output;
    if (!GetObjdumpOutput(binaryData, output)) {
        return false;
    }
 
    vector<string> lines;
    SplitString(output, '\n', lines);
    constexpr size_t kernelNameCol = 5;
    constexpr size_t kernelOffsetCol = 0;
    int badLineCnt = 0;
    for (string &line: lines) {
        istringstream iss(line);
        auto lineVec = vector<string>(istream_iterator<string>{iss},
                istream_iterator<string>());
        // dump info like this:
        // address      symbol   segment   type    size     kernelname
        // 0                1 2 3     4                5
        // 0000000000000000 g F .text 00000000000000a8 Abs_d2db1a80c523e7e59a032c95969880af_high_performance_2147483647
        if (lineVec.size() == 6 && lineVec.at(1) == "g" && lineVec.at(2) == "F" && lineVec.at(3) == ".text") {
            stringstream ss2(lineVec[kernelOffsetCol]);
            uint64_t offset {};
            ss2 >> std::hex >> offset;
            if (!ss2.fail()) {
                kernelOffsets.push_back(offset);
                kernelNames.push_back(lineVec.at(kernelNameCol));
            } else {
                badLineCnt++;
            }
        }
    }
    if (badLineCnt > 0) {
        WARN_LOG("%d lines had unparseable hex offset", badLineCnt);
    }
    return true;
}

bool GetSectionHeaders(rtDevBinary_t const &binary, map<string, Elf64_Shdr> &headers)
{
    if (binary.data == nullptr) {
        ERROR_LOG("empty dev binary info");
        return false;
    }

    auto binData = static_cast<char const *>(binary.data);
    vector<char> buffer(binData, binData + binary.length);
    return GetSectionHeaders(buffer, headers);
}

bool GetSectionHeaders(std::vector<char> const &binary, std::map<std::string, Elf64_Shdr> &headers)
{
    ElfLoader loader;
    if (!loader.FromBuffer(binary)) {
        ERROR_LOG("load elf from binary failed");
        return false;
    }

    Elf elf = loader.Load();
    headers = elf.GetSectionHeaders();
    return true;
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
    std::map<std::string, Elf64_Shdr> headers;
    if (!GetSectionHeaders(binary, headers)) {
        WARN_LOG("Get section headers failed");
        return 0;
    }
    auto dfxIter = headers.find(".ascend.meta." + kernelName);
    if (dfxIter == headers.end()) {
        DEBUG_LOG("Cannot find meta data in binary file");
        return 0;
    }
    Elf64_Off shOffset = dfxIter->second.sh_offset;
    Elf64_Xword	shSize = dfxIter->second.sh_size;
    if (shOffset + shSize > binary.length) {
        WARN_LOG("Meta data length error");
        return 0;
    }

    const uint8_t* charData = static_cast<const uint8_t*>(binary.data) + shOffset;
    std::vector<uint8_t> data(charData, charData + shSize);
    metaData.swap(data);
    return shSize;
}

bool GetSimtSymbolFromBinary(const char *data, uint64_t length)
{
    if (data == nullptr) {
        return false;
    }
    std::string binaryData(data, length);
    std::string objdumpOutput;
    if (!PipeCall({"llvm-objdump", "-t", "-"}, objdumpOutput, binaryData)) {
        DEBUG_LOG("Call llvm-objdump failed");
        return false;
    }
    vector<string> lines;
    SplitString(objdumpOutput, '\n', lines);
    for (string &line: lines) {
        istringstream iss(line);
        auto lineVec = vector<string>(istream_iterator<string>{iss}, istream_iterator<string>());
        // Simt symbol dump info like this:
        // 00000000000000d0 l   .text 0000000000000000 $t.1
        if (lineVec.size() == 5 && lineVec.at(1) == "l" && lineVec.at(2) == ".text" && StartsWith(lineVec.at(4), "$t")) {
            return true;
        }
    }
    return false;
}

RegisterContext::~RegisterContext()
{
    if (param_.needUnload) {
        aclrtBinaryUnLoadImplOrigin(hdl_);
    }
}

bool RegisterContext::Init(const vector<char> &elfData, aclrtBinHandle binHandle, const RegisterParam &param)
{
    elfData_ = elfData;
    param_ = param;
    if (!GetSymInfoFromBinary(elfData_.data(), elfData_.size(), kernelSymbolNames_, kernelOffsets_)) {
        DEBUG_LOG("Get symbol info from binary failed.");
        return false;
    }
    DEBUG_LOG("Get total %lu kernelNames", kernelSymbolNames_.size());
    hasSimt_ = GetSimtSymbolFromBinary(elfData_.data(), elfData_.size());
    rtDevBinary_t tempDevBin{};
    tempDevBin.data = elfData_.data();
    tempDevBin.length = elfData_.size();
    hasStaticStub_ = ::HasStaticStub(tempDevBin);
    hasDebugLine_ = ::HasDebugLine(tempDevBin);
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
    return Init(elfData, binHandle, param);
}

bool RegisterContext::Save(const string &outputPath) const
{
    Elf64_Ehdr header{};
    if (!ElfLoader::LoadHeader(elfData_, header)) {
        ERROR_LOG("Read elf header failed when load elf from buffer");
        return false;
    }
    auto elfData = elfData_;
    // 编译算子时没有指定target， flag值会为这个
    if (header.e_flags == FLAG_DEFAULT_VALUE) {
        auto correctFlag = GetFlagByMagic(param_.magic);
        if (correctFlag > 0) {
            header.e_flags = correctFlag;
            const char *base = static_cast<const char*>(static_cast<const void *>(&header));
            std::copy(base, base + sizeof(Elf64_Ehdr), elfData.data());
            DEBUG_LOG("default flag of op is forced set to %u", correctFlag);
        }
    }

    size_t written = WriteBinary(outputPath, elfData.data(), elfData.size());
    return written == elfData.size();
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
    if (!newRegCtx->Init(data, binHandle, newParam)) {
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

bool RegisterContext::HasSimtSymbol() const
{
    return hasSimt_;
}