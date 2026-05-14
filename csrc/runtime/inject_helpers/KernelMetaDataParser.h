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

#ifndef __KERNEL_META_DATA_PARSER_H__
#define __KERNEL_META_DATA_PARSER_H__

#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <functional>

#include "ascend_dump.h"
#include "utils/Protocol.h"
#include "DeviceContext.h"

struct AddrInfo {
    uint64_t addr;
    uint64_t length;
    MemInfoSrc memInfoSrc;
    MemInfoDesc memInfoDesc;
    uint64_t paramsNo;
    uint32_t permission;

    bool operator < (const AddrInfo &other) const
        {
            if (addr != other.addr) {
                return addr < other.addr;
            }
            if (length != other.length) {
                return length < other.length;
            }
            return static_cast<uint8_t>(memInfoSrc) < static_cast<uint8_t>(other.memInfoSrc);
        }
};

// 二级指针包含的一级指针信息
struct SecondPtrInfo {
    uint64_t dtypeBytes{};                     // 一级指针地址对应的数据dtype，解析.o时拿到
    std::vector<AddrInfo> addrInfoVec;         // 一级指针地址和dim信息，解析args中的hostInput拿到
};

// 这个结构体的信息来自meta数据和adump数据，理论上放到argsContext比较合适
// 但是meta数据有些属性跟args也无关，将来可以拆成两部分
struct OpMemInfo {
    // 内存信息<地址,长度, 来源>，算子入参的相关信息；不包括二级指针入参中的一级指针以及tiling
    std::vector<AddrInfo> inputParamsAddrInfos;
    // 二级指针<index, ...> key:index表示二级指针位于算子第几个入参，value表示二级指针包含的一级指针内存信息
    std::map<uint64_t, SecondPtrInfo> secondPtrAddrInfos;
    // 存储上述所有地址去重后的<addr, length, memInfoSrc>，防止double malloc 和 double free
    std::vector<AddrInfo> uniqueAddrInfos;
    uint32_t inputNum{};
    uint32_t skipNum{};
    uint64_t tilingDataSize{};
    bool isForSetException = false;
    bool isTik = false;
    bool hasMc2Ctx = false;
    bool isFFTS{false};
    bool hasOverflowAddr{false};
    uint64_t overflowAddr{};

    void Clear()
    {
        inputParamsAddrInfos.clear();
        secondPtrAddrInfos.clear();
        uniqueAddrInfos.clear();
        inputNum = 0;
        skipNum = 0;
        tilingDataSize = 0;
        isForSetException = false;
        isTik = false;
        hasMc2Ctx = false;
        isFFTS = false;
    }
};

inline bool NeedAddFftsAddr()
{
    std::string socVersion = DeviceContext::Local().GetSocVersion();
    if ((socVersion.find("Ascend310P") != std::string::npos) ||
        (socVersion.find("Ascend950") != std::string::npos)) {
        return false;
    }
    return true;
}

/// mix算子所有tilingKey对应的源码信息开头均包含ffts地址，但部分tilingKey下可能是vec算子，故mix算子的判断逻辑以二进制中的magic为准
inline void SetFftsInfo(OpMemInfo &memInfo, uint32_t magic = 0)
{
    if (!NeedAddFftsAddr()) {
        return;
    }
    bool isMix = memInfo.skipNum != 0 || magic == RT_DEV_BINARY_MAGIC_ELF;
    if (isMix) {
        memInfo.skipNum = 1;
        memInfo.isFFTS = true;
    } else {
        memInfo.isFFTS = false;
    }
}

class KernelMetaDataParser {
public:
    using ParseTableType = std::map<DfxTensorType, std::function<void(uint32_t &, uint32_t)>>;

    explicit KernelMetaDataParser(const std::vector<uint8_t> &metaData);

    bool Parse();

    const OpMemInfo &GetOpMemInfo() const { return memInfo_; }

private:
    void ParseMetaMc2Ctx(uint32_t &index, uint32_t paramsNo = 0);

    void ParseMetaInput(uint32_t &index, uint32_t paramsNo = 0);

    void ParseMetaFfts(uint32_t &index, uint32_t paramsNo = 0);

    void ParseMetaTiling(uint32_t &index, uint32_t paramsNo = 0);

    void ParseKernelArgs(uint32_t &index);
private:
    OpMemInfo memInfo_;
    std::vector<uint8_t> metaData_;
    ParseTableType dfxParser_;
};

#endif // __KERNEL_META_DATA_PARSER_H__