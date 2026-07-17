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

#include <unordered_map>
#include <mutex>
#include <vector>

#include "acl.h"
#include "runtime.h"
#include "ArgsContext.h"
#include "utils/Singleton.h"
#include "KernelMetaDataParser.h"

/// mc2_context gm地址位于算子kernel第几个index，不考虑ffts地址
constexpr uint32_t MC2_CONTEXT_PARAMS_INDEX = 0U;
void UpdateInputSizeWithTilingData(const std::vector<uint8_t> &tilingData, OpMemInfo &memInfo);

struct AdumpInfo {
    void *addr;
    uint32_t argsSpace;
};
/*
 * 功能：管理每次KernelLaunch时的ArgsContext信息.
 * 当前接口：支持创建、获取aclrt流程里的参数上下文
 */
class ArgsManager : public Singleton<ArgsManager, false> {
public:
    // aclrt接口里的ArgsHandleContext创建
    ArgsContextSP CreateContext(aclrtFuncHandle funcHandle, aclrtArgsHandle argsHandle);
    ArgsContextSP CreateContext(void *args, uint32_t argsSize, bool isDeviceArgs = false);

    ArgsContextSP CreateContext(void *args, uint32_t argsSize, const std::vector<aclrtPlaceHolderInfo> &placeHolderArray);
    // aclrt接口里的ArgsHandleContext获取
    ArgsContextSP GetContext(aclrtArgsHandle argsHandle) const;

    bool GetAdumpInfo(uint64_t index, AdumpInfo &info) const;

    void AddAdumpInfo(uint64_t index, const AdumpInfo &info);

    const AdumpInfo &GetLatestAdumpInfo() const;

    bool HasAdumpInfo() const;

    // 设置当前算子是否经过adump接口标识
    void SetThroughAdumpFlag(bool flag) { isThroughAdump_ = flag; }

    // 获取当前算子是否经过adump接口标识
    bool GetThroughAdumpFlag() const { return isThroughAdump_; }

    /// 解析二级指针地址信息，index表示位于args中的第几个入参
    void ParseSecondPtrAddrs(const AclrtLaunchArgsInfo &launchArgs, OpMemInfo &opMemInfo, uint32_t index) const;

    /// 设置算子的argsSize
    void SetArgsSize(uint32_t argSize) { argsSize_ = argSize; }
    /// 获取算子的argsSize
    uint32_t GetArgsSize() const { return argsSize_; }
    /// 解析mc2算子共享内存地址信息
    std::vector<AddrInfo> GetMc2AddrInfosFromArgAddr(uint64_t addr) const;

#if defined (__BUILD_TESTS__)
    void Clear()
    {
        contexts_.clear();
        atomicIndexToAddr_.clear();
        isThroughAdump_ = true;
        latestAtomicIndex_ = 0;
         argsSize_ = 0;
    }
#endif

private:
    /// 解析mc2算子共享内存地址信息
    std::vector<AddrInfo> GetMc2AddrInfosFromArgAddr910B(uint64_t addr) const;

    /// 解析910C mc2算子(HcclOpResParam)共享内存地址信息，遍历remoteRes拿到各remote rank的windowsIn地址
    std::vector<AddrInfo> GetMc2AddrInfosFromArgAddr910C(uint64_t addr) const;

private:
    std::unordered_map<aclrtArgsHandle, ArgsContextSP> contexts_;
    std::unordered_map<uint64_t, AdumpInfo> atomicIndexToAddr_;
    uint64_t latestAtomicIndex_{0};
    mutable std::mutex adumpInfoMtx_;
    bool isThroughAdump_{true};    // 算子运行时是否经过了adump开关
    uint32_t argsSize_{};
};
