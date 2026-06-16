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


#include "KernelMetaDataParser.h"

#include "utils/InjectLogger.h"
#include "hccl.h"

namespace {
constexpr uint8_t U64_BYTE_SIZE = 8U;

std::string DfxTensorTypeToString(DfxTensorType type)
{
    switch (type) {
        case DfxTensorType::INVALID_TENSOR:      return "invalid_tensor";
        case DfxTensorType::GENERAL_TENSOR:      return "general_tensor";
        case DfxTensorType::INPUT_TENSOR:        return "input_tensor";
        case DfxTensorType::OUTPUT_TENSOR:       return "output_tensor";
        case DfxTensorType::WORKSPACE_TENSOR:    return "workspace_tensor";
        case DfxTensorType::ASCENDC_LOG:         return "ascendc_log";
        case DfxTensorType::MC2_CTX:             return "mc2_ctx";
        case DfxTensorType::TILING_DATA:         return "tiling_data";
        case DfxTensorType::L1:                  return "l1";
        case DfxTensorType::L2:                  return "l2";
        case DfxTensorType::OVERFLOW_MES:        return "overflow_mes";
        case DfxTensorType::FFTS_ADDRESS:        return "ffts_address";
        case DfxTensorType::SHAPE_TENSOR:        return "shape_tensor";
    }
    return "unknown";
}

bool IsSupportDfxPointPtr(DfxPointerType type)
{
    switch (type) {
        case DfxPointerType::LEVEL_1_POINTER:
        case DfxPointerType::LEVEL_2_POINTER_WITH_SHAPE:
        case DfxPointerType::SHAPE_TENSOR_PLACEHOLD:
            return true;
        case DfxPointerType::INVALID_POINTER:
        case DfxPointerType::LEVEL_2_POINTER:
        default:
            return false;
    }
}
}

bool KernelMetaDataParser::Parse()
{
    auto shSize = metaData_.size();
    if (shSize == 0) {
        WARN_LOG("Parse meta data failed, empty metadata");
        return false;
    }
    uint32_t index {0U};
    while (index < shSize - 1) {
        // 2个字节的小端表示当前type类型，当前仅仅处理4和6，4表示meta dfx数据需要解析，6表示当前算子为tik算子，解析atomic时需要特殊处理
        auto type = static_cast<DfxType>(metaData_[index] + (metaData_[index + 1] << 8));
        index += 2; // 加2之后移动到最新的index
        // 加2校验长度信息
        if (index + 2 > shSize) {
            WARN_LOG("Parse meta data failed");
            return false;
        }
        if (type == DfxType::DFX_TYPE) {
            ParseKernelArgs(index);
            if (memInfo_.inputParamsAddrInfos.empty()) {
                WARN_LOG("Parse kernel args failed");
                return false;
            }
            continue;
        }
        if (type == DfxType::TIK_TYPE) {
            memInfo_.isTik = true;
        }
        // 2个字节的小端表示当前type后跟的字节数
        auto length = static_cast<uint32_t>(metaData_[index] + (metaData_[index + 1] << 8));
        // 如果当前type的内容不需要读取，则需要跳过，2是length的长度
        index += (2 + length);
        DEBUG_LOG("Dfx type is %u, length: %u", static_cast<uint32_t>(type), static_cast<uint32_t>(length));
    }
    return true;
}

KernelMetaDataParser::KernelMetaDataParser(const std::vector<uint8_t> &metaData) : metaData_(metaData)
{
    memInfo_.Clear();
    dfxParser_[DfxTensorType::GENERAL_TENSOR] = std::bind(&KernelMetaDataParser::ParseMetaInput, this,
        std::placeholders::_1, std::placeholders::_2);
    dfxParser_[DfxTensorType::INPUT_TENSOR] = std::bind(&KernelMetaDataParser::ParseMetaInput, this,
        std::placeholders::_1, std::placeholders::_2);
    dfxParser_[DfxTensorType::OUTPUT_TENSOR] = std::bind(&KernelMetaDataParser::ParseMetaInput, this,
        std::placeholders::_1, std::placeholders::_2);
    dfxParser_[DfxTensorType::WORKSPACE_TENSOR] = std::bind(&KernelMetaDataParser::ParseMetaInput, this,
        std::placeholders::_1, std::placeholders::_2);
    dfxParser_[DfxTensorType::FFTS_ADDRESS] = std::bind(&KernelMetaDataParser::ParseMetaFfts, this,
        std::placeholders::_1, std::placeholders::_2);
    dfxParser_[DfxTensorType::TILING_DATA] = std::bind(&KernelMetaDataParser::ParseMetaTiling, this,
        std::placeholders::_1, std::placeholders::_2);
    dfxParser_[DfxTensorType::MC2_CTX] = std::bind(&KernelMetaDataParser::ParseMetaMc2Ctx, this,
        std::placeholders::_1, std::placeholders::_2);
    dfxParser_[DfxTensorType::SHAPE_TENSOR] = std::bind(&KernelMetaDataParser::ParseMetaInput, this,
        std::placeholders::_1, std::placeholders::_2);
}

void KernelMetaDataParser::ParseMetaInput(uint32_t &index, uint32_t paramsNo)
{
    if (metaData_.size() < (U64_BYTE_SIZE + index)) {
        WARN_LOG("Can not Parse Meta input data, index error");
        return;
    }
    uint64_t length = 0;
    // 取uint64的数据
    for (size_t i = 0; i < U64_BYTE_SIZE; ++i) {
        length |= static_cast<uint64_t>(metaData_[i + index]) << ((U64_BYTE_SIZE - 1 - i) * U64_BYTE_SIZE);
    }
    memInfo_.inputParamsAddrInfos.emplace_back(AddrInfo{0U, length, MemInfoSrc::EXTRA,
        MemInfoDesc::INPUT, paramsNo});
    DEBUG_LOG("Get %zu data, length is %lu", memInfo_.inputParamsAddrInfos.size(), static_cast<uint64_t>(length));
}

void KernelMetaDataParser::ParseMetaMc2Ctx(uint32_t &index, uint32_t paramsNo)
{
    DEBUG_LOG("has mc2ctx input");
    (void)metaData_;
    (void)index;
    (void)paramsNo;
    memInfo_.inputParamsAddrInfos.emplace_back(AddrInfo{0U, sizeof(HcclCombinOpParam), MemInfoSrc::EXTRA,
        MemInfoDesc::HCCL_MC2_CONTEXT});
    memInfo_.hasMc2Ctx = true;
}

void KernelMetaDataParser::ParseMetaFfts(uint32_t &index, uint32_t paramsNo)
{
    (void)index;
    (void)paramsNo;
    SetFftsInfo(memInfo_);
}

void KernelMetaDataParser::ParseMetaTiling(uint32_t &index, uint32_t paramsNo)
{
    (void)paramsNo;
    if (metaData_.size() < (U64_BYTE_SIZE + index)) {
        WARN_LOG("Can not Parse Meta tiling data, index error");
        return;
    }
    // 取uint64的数据
    memInfo_.tilingDataSize = 0;
    for (size_t i = 0; i < U64_BYTE_SIZE; ++i) {
        memInfo_.tilingDataSize |=
            static_cast<uint64_t>(metaData_[i + index]) << ((U64_BYTE_SIZE - 1 - i) * U64_BYTE_SIZE);
    }
    DEBUG_LOG("Get tiling data, data length is %lu", static_cast<uint64_t>(memInfo_.tilingDataSize));
}

void KernelMetaDataParser::ParseKernelArgs(uint32_t &index)
{
    // 2个字节的小端表示后面有多少个字节表示dfx信息，index + 1位置需要左移8位
    auto dfxLen = static_cast<uint32_t>(metaData_[index] + (metaData_[index + 1] << 8));
    index += 2; // 加2之后移动到最新的index
    uint32_t dfxRange = dfxLen + index;
    uint32_t paramsIdx{};
    while (index < dfxRange && (index + 7 < metaData_.size())) {
        // 2个字节的大端表示当前参数的dfx信息由几个8字节组成，index + 2位置需要左移8位，index + 3不动
        auto numOfUint64 = static_cast<uint32_t>((metaData_[index + 2] << 8) + (metaData_[index + 3]));
        if (numOfUint64 == 0) {
            DEBUG_LOG("number Of uint64 is 0");
            index += 4;
            continue;
        }
        index += 4; // 加4之后移动到最新的index
        // 2个字节的大端表示当前参数的类型，index + 6位置需要左移8位，index + 7不动
        auto type = static_cast<DfxTensorType>((metaData_[index + 6] << 8) + metaData_[index + 7]);
        // 2个字节的大端表示当前参数的指针类型，index + 4位置需要左移8位，index + 5不动
        auto ptrType = static_cast<DfxPointerType>((metaData_[index + 4] << 8) + metaData_[index + 5]);
        DEBUG_LOG("Get Meta data, data type: %s, number Of uint64 is: %u",
                  DfxTensorTypeToString(type).c_str(), static_cast<uint32_t>(numOfUint64));
        auto funcIter = dfxParser_.find(type);
        if (funcIter == dfxParser_.end()) {
            index += (numOfUint64 * U64_BYTE_SIZE);
            DEBUG_LOG("Cannot find dfx type, type is %u", static_cast<uint32_t>(type));
            break;
        }
        if (!IsSupportDfxPointPtr(ptrType)) {
            memInfo_.Clear();
            INFO_LOG("unsupported level pointer, type is %u", static_cast<uint32_t>(ptrType));
            return;
        }
        if (ptrType == DfxPointerType::LEVEL_2_POINTER_WITH_SHAPE) {
            DEBUG_LOG("the %u parameter of the kernel is a double pointer", paramsIdx);
            uint64_t dtypeBytes = 0;
            // 二级指针地址往后偏移16个字节为一级指针的dtype
            for (size_t i = 0; i < U64_BYTE_SIZE; ++i) {
                dtypeBytes |= static_cast<uint64_t>(metaData_[i + index + 16]) << ((U64_BYTE_SIZE - 1 - i) * U64_BYTE_SIZE);
            }
            SecondPtrInfo secondPtrInfo{};
            secondPtrInfo.dtypeBytes = dtypeBytes;
            memInfo_.secondPtrAddrInfos.insert({paramsIdx, secondPtrInfo});
        }
        index += 8; // 加8之后移动到最新的index
        funcIter->second(index, paramsIdx + 1);
        index += ((numOfUint64 - 1) * U64_BYTE_SIZE);
        paramsIdx++;
    }
}
