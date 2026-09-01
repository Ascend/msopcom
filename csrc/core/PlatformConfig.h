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


#ifndef __CORE_PLATFORM_CONFIG_H__
#define __CORE_PLATFORM_CONFIG_H__

#include <cstdint>
#include <string>
#include <unordered_map>

/* 此文件定义所有芯片平台和硬件信息相关的枚举和结构，
 * 所有工具复用此文件中的枚举
 */
constexpr int RADIX = 8;

enum class DeviceType : uint32_t {
    ASCEND_910_PREMIUM_A = 0U,

    ASCEND_910B_START,
    ASCEND_910B1,
    ASCEND_910B2,
    ASCEND_910B3,
    ASCEND_910B4,
    ASCEND_910B_END,

    ASCEND_310P,

    ASCEND_950_START = 10,
    ASCEND_950DT_950x,
    ASCEND_950DT_950y,
    ASCEND_950DT_9571,
    ASCEND_950DT_9572,
    ASCEND_950DT_9573,
    ASCEND_950DT_9574,
    ASCEND_950DT_9575,
    ASCEND_950DT_9576,
    ASCEND_950DT_9577,
    ASCEND_950DT_9578,
    ASCEND_950DT_9581,
    ASCEND_950DT_9582,
    ASCEND_950DT_9583,
    ASCEND_950DT_9584,
    ASCEND_950DT_9585,
    ASCEND_950DT_9586,
    ASCEND_950DT_9587,
    ASCEND_950DT_9588,
    ASCEND_950DT_9591,
    ASCEND_950DT_9592,
    ASCEND_950DT_9595,
    ASCEND_950DT_9596,
    ASCEND_950DT_95A1,
    ASCEND_950DT_95A2,
    ASCEND_950PR_950z,
    ASCEND_950PR_9579,
    ASCEND_950PR_957b,
    ASCEND_950PR_957c,
    ASCEND_950PR_957d,
    ASCEND_950PR_9589,
    ASCEND_950PR_958b,
    ASCEND_950PR_9599,
    ASCEND_950_END,

    INVALID = 0xFFFFFFFFU,
 };

enum class KernelType : uint8_t {
    AICPU = 0,        /// 对应 RTS 中的 RT_DEV_BINARY_MAGIC_ELF_AICPU
    AIVEC,            /// 对应 RTS 中的 RT_DEV_BINARY_MAGIC_ELF_AIVEC
    AICUBE,           /// 对应 RTS 中的 RT_DEV_BINARY_MAGIC_ELF_AICUBE
    MIX,              /// 对应 RTS 中的 RT_DEV_BINARY_MAGIC_ELF
    INVALID,
};

enum class ChipProductType : uint32_t {
    ALL_PRODUCT_TYPE = 0,
    UNKNOWN_PRODUCT_TYPE = 1,

    ASCEND310P_SERIES = 1 << RADIX, // ASCEND310P series product type
    ASCEND310P1,
    ASCEND310P2,
    ASCEND310P3,
    ASCEND310P4,
    ASCEND310P5,
    ASCEND310P7,

    ASCEND310B_SERIES = 2 << RADIX, // ASCEND310B series product type
    ASCEND310B1,
    ASCEND310B2,
    ASCEND310B3,
    ASCEND310B4,

    ASCEND910B_SERIES = 3 << RADIX, // ASCEND910B series product type
    ASCEND910B1,
    ASCEND910B2,
    ASCEND910B3,
    ASCEND910B4,
    ASCEND910B2C,
    ASCEND910B4_1,

    ASCEND910_93_SERIES = 4 << RADIX, // ASCEND910_93 series product type
    ASCEND910_9391,
    ASCEND910_9392,
    ASCEND910_9381,
    ASCEND910_9382,
    ASCEND910_9372,
    ASCEND910_9362,
    ASCEND910_9363,

    ASCEND310_SERIES = 5 << RADIX,
    ASCEND310,

    ASCEND910A_SERIES = 6 << RADIX,
    ASCEND910A,
    ASCEND910B,
    ASCEND910PROA,
    ASCEND910PROB,
    ASCEND910PREMIUMA,

    ASCEND610_SERIES = 7 << RADIX,
    ASCEND615_SERIES = 8 << RADIX,

    ASCEND950_SERIES = 9 << RADIX,
    ASCEND950DT_950X,
    ASCEND950DT_950Y,
    ASCEND950DT_9571,
    ASCEND950DT_9572,
    ASCEND950DT_9573,
    ASCEND950DT_9574,
    ASCEND950DT_9575,
    ASCEND950DT_9576,
    ASCEND950DT_9577,
    ASCEND950DT_9578,
    ASCEND950DT_9581,
    ASCEND950DT_9582,
    ASCEND950DT_9583,
    ASCEND950DT_9584,
    ASCEND950DT_9585,
    ASCEND950DT_9586,
    ASCEND950DT_9587,
    ASCEND950DT_9588,
    ASCEND950DT_9591,
    ASCEND950DT_9592,
    ASCEND950DT_9595,
    ASCEND950DT_9596,
    ASCEND950DT_95A1,
    ASCEND950DT_95A2,
    ASCEND950PR_950Z,
    ASCEND950PR_9579,
    ASCEND950PR_957B,
    ASCEND950PR_957C,
    ASCEND950PR_957D,
    ASCEND950PR_9589,
    ASCEND950PR_958B,
    ASCEND950PR_9599,
};

const std::unordered_map<std::string, ChipProductType> SOC_STRING_TO_CHIP_PRODUCT{
    {"Ascend910B1", ChipProductType::ASCEND910B1},
    {"Ascend910B2", ChipProductType::ASCEND910B2},
    {"Ascend910B3", ChipProductType::ASCEND910B3},
    {"Ascend910B4", ChipProductType::ASCEND910B4},
    {"Ascend910B2C", ChipProductType::ASCEND910B2C},
    {"Ascend910B4-1", ChipProductType::ASCEND910B4_1},

    {"Ascend910_9391", ChipProductType::ASCEND910_9391},
    {"Ascend910_9392", ChipProductType::ASCEND910_9392},
    {"Ascend910_9381", ChipProductType::ASCEND910_9381},
    {"Ascend910_9382", ChipProductType::ASCEND910_9382},
    {"Ascend910_9372", ChipProductType::ASCEND910_9372},
    {"Ascend910_9362", ChipProductType::ASCEND910_9362},
    {"Ascend910_9363", ChipProductType::ASCEND910_9363},

    {"Ascend310B1", ChipProductType::ASCEND310B1},
    {"Ascend310B2", ChipProductType::ASCEND310B2},
    {"Ascend310B3", ChipProductType::ASCEND310B3},
    {"Ascend310B4", ChipProductType::ASCEND310B4},

    {"Ascend310P1", ChipProductType::ASCEND310P1},
    {"Ascend310P2", ChipProductType::ASCEND310P2},
    {"Ascend310P3", ChipProductType::ASCEND310P3},
    {"Ascend310P4", ChipProductType::ASCEND310P4},
    {"Ascend310P5", ChipProductType::ASCEND310P5},
    {"Ascend310P7", ChipProductType::ASCEND310P7},

    {"Ascend310", ChipProductType::ASCEND310},
    {"Ascend910A", ChipProductType::ASCEND910A},
    {"Ascend910B", ChipProductType::ASCEND910B},
    {"Ascend910ProA", ChipProductType::ASCEND910PROA},
    {"Ascend910ProB", ChipProductType::ASCEND910PROB},
    {"AscendPremiumA", ChipProductType::ASCEND910PREMIUMA},

    {"Ascend950DT_950x", ChipProductType::ASCEND950DT_950X},
    {"Ascend950DT_950y", ChipProductType::ASCEND950DT_950Y},
    {"Ascend950DT_9571", ChipProductType::ASCEND950DT_9571},
    {"Ascend950DT_9572", ChipProductType::ASCEND950DT_9572},
    {"Ascend950DT_9573", ChipProductType::ASCEND950DT_9573},
    {"Ascend950DT_9574", ChipProductType::ASCEND950DT_9574},
    {"Ascend950DT_9575", ChipProductType::ASCEND950DT_9575},
    {"Ascend950DT_9576", ChipProductType::ASCEND950DT_9576},
    {"Ascend950DT_9577", ChipProductType::ASCEND950DT_9577},
    {"Ascend950DT_9578", ChipProductType::ASCEND950DT_9578},
    {"Ascend950DT_9581", ChipProductType::ASCEND950DT_9581},
    {"Ascend950DT_9582", ChipProductType::ASCEND950DT_9582},
    {"Ascend950DT_9583", ChipProductType::ASCEND950DT_9583},
    {"Ascend950DT_9584", ChipProductType::ASCEND950DT_9584},
    {"Ascend950DT_9585", ChipProductType::ASCEND950DT_9585},
    {"Ascend950DT_9586", ChipProductType::ASCEND950DT_9586},
    {"Ascend950DT_9587", ChipProductType::ASCEND950DT_9587},
    {"Ascend950DT_9588", ChipProductType::ASCEND950DT_9588},
    {"Ascend950DT_9591", ChipProductType::ASCEND950DT_9591},
    {"Ascend950DT_9592", ChipProductType::ASCEND950DT_9592},
    {"Ascend950DT_9595", ChipProductType::ASCEND950DT_9595},
    {"Ascend950DT_9596", ChipProductType::ASCEND950DT_9596},
    {"Ascend950DT_95A1", ChipProductType::ASCEND950DT_95A1},
    {"Ascend950DT_95A2", ChipProductType::ASCEND950DT_95A2},
    {"Ascend950PR_950z", ChipProductType::ASCEND950PR_950Z},
    {"Ascend950PR_9579", ChipProductType::ASCEND950PR_9579},
    {"Ascend950PR_957b", ChipProductType::ASCEND950PR_957B},
    {"Ascend950PR_957c", ChipProductType::ASCEND950PR_957C},
    {"Ascend950PR_957d", ChipProductType::ASCEND950PR_957D},
    {"Ascend950PR_9589", ChipProductType::ASCEND950PR_9589},
    {"Ascend950PR_958b", ChipProductType::ASCEND950PR_958B},
    {"Ascend950PR_9599", ChipProductType::ASCEND950PR_9599},
};

const std::unordered_map<ChipProductType, DeviceType> CHIP_PRODUCT_TO_DEVICE_TYPE = {
    {ChipProductType::ASCEND910PREMIUMA, DeviceType::ASCEND_910_PREMIUM_A},
    {ChipProductType::ASCEND910B1, DeviceType::ASCEND_910B1},
    {ChipProductType::ASCEND910B2, DeviceType::ASCEND_910B2},
    {ChipProductType::ASCEND910B2C, DeviceType::ASCEND_910B2},
    {ChipProductType::ASCEND910B3, DeviceType::ASCEND_910B3},
    {ChipProductType::ASCEND910B4, DeviceType::ASCEND_910B4},
    {ChipProductType::ASCEND910B4_1, DeviceType::ASCEND_910B4},
    {ChipProductType::ASCEND910_9391, DeviceType::ASCEND_910B1},
    {ChipProductType::ASCEND910_9392, DeviceType::ASCEND_910B1},
    {ChipProductType::ASCEND910_9381, DeviceType::ASCEND_910B2},
    {ChipProductType::ASCEND910_9382, DeviceType::ASCEND_910B2},
    {ChipProductType::ASCEND910_9372, DeviceType::ASCEND_910B3},
    {ChipProductType::ASCEND910_9362, DeviceType::ASCEND_910B4},
    {ChipProductType::ASCEND910_9363, DeviceType::ASCEND_910B4},
    {ChipProductType::ASCEND310P1, DeviceType::ASCEND_310P},
    {ChipProductType::ASCEND310P3, DeviceType::ASCEND_310P},
    {ChipProductType::ASCEND310P5, DeviceType::ASCEND_310P},
    {ChipProductType::ASCEND310P7, DeviceType::ASCEND_310P},
    {ChipProductType::ASCEND950DT_950X, DeviceType::ASCEND_950DT_950x},
    {ChipProductType::ASCEND950DT_950Y, DeviceType::ASCEND_950DT_950y},
    {ChipProductType::ASCEND950DT_9571, DeviceType::ASCEND_950DT_9571},
    {ChipProductType::ASCEND950DT_9572, DeviceType::ASCEND_950DT_9572},
    {ChipProductType::ASCEND950DT_9573, DeviceType::ASCEND_950DT_9573},
    {ChipProductType::ASCEND950DT_9574, DeviceType::ASCEND_950DT_9574},
    {ChipProductType::ASCEND950DT_9575, DeviceType::ASCEND_950DT_9575},
    {ChipProductType::ASCEND950DT_9576, DeviceType::ASCEND_950DT_9576},
    {ChipProductType::ASCEND950DT_9577, DeviceType::ASCEND_950DT_9577},
    {ChipProductType::ASCEND950DT_9578, DeviceType::ASCEND_950DT_9578},
    {ChipProductType::ASCEND950DT_9581, DeviceType::ASCEND_950DT_9581},
    {ChipProductType::ASCEND950DT_9582, DeviceType::ASCEND_950DT_9582},
    {ChipProductType::ASCEND950DT_9583, DeviceType::ASCEND_950DT_9583},
    {ChipProductType::ASCEND950DT_9584, DeviceType::ASCEND_950DT_9584},
    {ChipProductType::ASCEND950DT_9585, DeviceType::ASCEND_950DT_9585},
    {ChipProductType::ASCEND950DT_9586, DeviceType::ASCEND_950DT_9586},
    {ChipProductType::ASCEND950DT_9587, DeviceType::ASCEND_950DT_9587},
    {ChipProductType::ASCEND950DT_9588, DeviceType::ASCEND_950DT_9588},
    {ChipProductType::ASCEND950DT_9591, DeviceType::ASCEND_950DT_9591},
    {ChipProductType::ASCEND950DT_9592, DeviceType::ASCEND_950DT_9592},
    {ChipProductType::ASCEND950DT_9595, DeviceType::ASCEND_950DT_9595},
    {ChipProductType::ASCEND950DT_9596, DeviceType::ASCEND_950DT_9596},
    {ChipProductType::ASCEND950DT_95A1, DeviceType::ASCEND_950DT_95A1},
    {ChipProductType::ASCEND950DT_95A2, DeviceType::ASCEND_950DT_95A2},
    {ChipProductType::ASCEND950PR_950Z, DeviceType::ASCEND_950PR_950z},
    {ChipProductType::ASCEND950PR_9579, DeviceType::ASCEND_950PR_9579},
    {ChipProductType::ASCEND950PR_957B, DeviceType::ASCEND_950PR_957b},
    {ChipProductType::ASCEND950PR_957C, DeviceType::ASCEND_950PR_957c},
    {ChipProductType::ASCEND950PR_957D, DeviceType::ASCEND_950PR_957d},
    {ChipProductType::ASCEND950PR_9589, DeviceType::ASCEND_950PR_9589},
    {ChipProductType::ASCEND950PR_958B, DeviceType::ASCEND_950PR_958b},
    {ChipProductType::ASCEND950PR_9599, DeviceType::ASCEND_950PR_9599},
};

inline ChipProductType GetProductSeriesType(const ChipProductType& chipProductType)
{
    return static_cast<ChipProductType>((static_cast<uint32_t>(chipProductType) >> RADIX) << RADIX);
}


inline bool IsChipSeriesTypeValid(const ChipProductType& inputType, const ChipProductType& requiredType)
{
    if (requiredType == ChipProductType::ALL_PRODUCT_TYPE) {
        return true;
    }
    return GetProductSeriesType(inputType) == requiredType;
}

inline ChipProductType GetProductTypeBySocVersion(const std::string &socVersion)
{
    auto it = SOC_STRING_TO_CHIP_PRODUCT.find(socVersion);
    ChipProductType chipType = it == SOC_STRING_TO_CHIP_PRODUCT.end() ? ChipProductType::UNKNOWN_PRODUCT_TYPE : it->second;
    return chipType;
}

inline ChipProductType GetProductSeriesType(const std::string &socVersion)
{
    ChipProductType chipProductType = GetProductTypeBySocVersion(socVersion);
    if (chipProductType == ChipProductType::UNKNOWN_PRODUCT_TYPE) {
        return chipProductType;
    }
    return GetProductSeriesType(chipProductType);
}

inline DeviceType GetDeviceTypeBySocVersion(const std::string &socVersion)
{
    auto it = SOC_STRING_TO_CHIP_PRODUCT.find(socVersion);
    ChipProductType chipType = it == SOC_STRING_TO_CHIP_PRODUCT.end() ? ChipProductType::UNKNOWN_PRODUCT_TYPE : it->second;
    auto it2 = CHIP_PRODUCT_TO_DEVICE_TYPE.find(chipType);
    DeviceType deviceType = it2 == CHIP_PRODUCT_TO_DEVICE_TYPE.end() ? DeviceType::INVALID : it2->second;
    return deviceType;
}

// mix算子运行时，blockDim = 1对应1个cube + 2个vec
/// 因某些硬件的特殊架构，一个 AICore 中包含若干个 AIVEC 核和 AICUBE 核（c220架构 为 1 个 AICUBE 核和 2 个 AIVEC 核）
/// 当算子以 MIX 模式运行时，blockDim 代表参与运算的 AICore 逻辑核数，因此实际参与运算的 subBlockDim 分别为
/// blockDim * C220_VEC_SUB_BLOCKDIM 和 blockDim * C220_CUBE_SUB_BLOCKDIM。
// 最终申请的GM内存为：RECORD_BUF_SIZE_EACH_BLOCK * blockDim * (C220_VEC_SUB_BLOCKDIM + C220_CUBE_SUB_BLOCKDIM)
// 排布顺序为：vec/vec/cube/vec/vec/cube ......
constexpr uint8_t C220_VEC_SUB_BLOCKDIM = 2;
constexpr uint8_t C220_CUBE_SUB_BLOCKDIM = 1;
constexpr uint8_t C220_MIX_SUB_BLOCKDIM = C220_VEC_SUB_BLOCKDIM + C220_CUBE_SUB_BLOCKDIM;

// MB转换到字节换算单位
constexpr uint64_t MB_TO_BYTES = 1024 * 1024UL;

// mssanitizer单核内存最大8G，单位为M
constexpr uint16_t MAX_RECORD_BUF_SIZE_EACH_BLOCK = 8 * 1024;

// mssanitizer多核内存最大24G，单位为M
constexpr uint16_t MAX_RECORD_BUF_SIZE_ALL_BLOCK = 24 * 1024;

// 单核检测时不检测的核需要分配的内存大小，单位为M
constexpr uint16_t SINGLE_CHECK_OTHER_BLOCK_CACHE_SIZE = 1;

#endif // __CORE_PLATFORM_CONFIG_H__
