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

#ifndef __LAUNCH_MANAGER_H__
#define __LAUNCH_MANAGER_H__

#pragma once

#include <unordered_map>
#include <mutex>

#include "acl.h"
#include "runtime.h"
#include "LaunchContext.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "runtime/inject_helpers/KernelMetaDataParser.h"

struct StreamInfo {
    bool binded{false};
    bool subscribed{false};
};

/*
 * 此类用于管理LaunchContext，后续很多KernelContext的信息都可以慢慢移过来。
 * 该类需要依赖FuncMansger和ArgsManager类来创建LaunchContext
 * 该类数据的读写需要考虑支持多卡场景，考虑兼容，此类是给每个卡提供一个单例。
 * 如果有些资源是全局所有的，请用static修饰这些资源，并提供static的成员方法来获取，
 * 当前这些资源并不多，后续可以按需剥离出这些全局资源到特定业务类.
 */
class LaunchManager {
public:
    // 每个device一个Instance
    static LaunchManager &Local();

    LaunchContextSP CreateContext(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream,
                                   aclrtLaunchKernelCfg *cfg, aclrtArgsHandle argsHandle);

    LaunchContextSP CreateContext(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream,
                                   aclrtLaunchKernelCfg *cfg, ArgsContextSP argsContext);

    LaunchContextSP GetContext(uint64_t launchId) const;

    LaunchContextSP GetLastContext() const;

    // 由于类本身是非全局单例，全局资源的使用方法需要static修饰
    static StreamInfo &GetOrCreateStreamInfo(aclrtStream stm) { return streamInfos_[stm]; }

    OpMemInfo &GetCurrentMemInfo() { return memInfo_; }

    std::vector<OpMemInfo> &GetMemInfoHistory() { return memInfoHistory_; }

    void ArchiveMemInfo() { memInfoHistory_.push_back(memInfo_); }

    void SetSimtUbDynamicSize(uint32_t simtUbDynamicSize) { simtUbDynamicSize_ = simtUbDynamicSize; }

    uint32_t GetSimtUbDynamicSize() const { return simtUbDynamicSize_; }

#if defined (__BUILD_TESTS__)
    void Clear()
    {
        contexts_.clear();
        streamInfos_.clear();
        memInfo_.Clear();
        memInfoHistory_.clear();
        simtUbDynamicSize_ = 0;
    }
#endif
private:
    std::vector<LaunchContextSP> contexts_;
    // 由于类本身是非全局单例，全局资源需要static修饰
    static std::unordered_map<aclrtStream, StreamInfo> streamInfos_;
    // 这里的信息有部分依赖kernelName去解析meta段以及需要每张卡一个，所以放这里合适
    OpMemInfo memInfo_;
    std::vector<OpMemInfo> memInfoHistory_;
    uint32_t simtUbDynamicSize_{};
};

#endif // __LAUNCH_MANAGER_H__
