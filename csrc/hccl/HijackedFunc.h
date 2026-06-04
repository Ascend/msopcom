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

// 该文件定义劫持函数对象
// 这个文件中尽量不要引入各种私有定义，保持依赖的干净

#ifndef __HCCL_HIJACKED_FUNC_H__
#define __HCCL_HIJACKED_FUNC_H__

#include "hccl.h"
#include "core/HijackedFuncTemplate.h"

template <> struct EmptyFuncError<HcclResult> {
    // `HCCL_E_INTERNAL' 用于代表 HcclResult类型中internal error
    static constexpr HcclResult VALUE = HCCL_E_INTERNAL;
};

class HijackedFuncOfHcclCommInitClusterInfo : public decltype(HijackedFuncHelper(&HcclCommInitClusterInfo)) {
public:
    explicit HijackedFuncOfHcclCommInitClusterInfo();
    ~HijackedFuncOfHcclCommInitClusterInfo() override = default;
    void Pre(const char *clusterInfo, uint32_t rank, HcclComm *comm) override;
    HcclResult Post(HcclResult ret) override;

private:
    HcclComm *comm_{nullptr};
};

class HijackedFuncOfHcclCommInitClusterInfoConfig
    : public decltype(HijackedFuncHelper(&HcclCommInitClusterInfoConfig)) {
public:
    explicit HijackedFuncOfHcclCommInitClusterInfoConfig();
    ~HijackedFuncOfHcclCommInitClusterInfoConfig() override = default;
    void Pre(const char *clusterInfo, uint32_t rank, HcclCommConfig *config, HcclComm *comm) override;
    HcclResult Post(HcclResult ret) override;

private:
    HcclComm *comm_{nullptr};
};

class HijackedFuncOfHcclCommInitRootInfo : public decltype(HijackedFuncHelper(&HcclCommInitRootInfo)) {
public:
    explicit HijackedFuncOfHcclCommInitRootInfo();
    ~HijackedFuncOfHcclCommInitRootInfo() override = default;
    void Pre(uint32_t nRanks, const HcclRootInfo *rootInfo, uint32_t rank, HcclComm *comm) override;
    HcclResult Post(HcclResult ret) override;

private:
    HcclComm *comm_{nullptr};
};

class HijackedFuncOfHcclCommInitRootInfoConfig : public decltype(HijackedFuncHelper(&HcclCommInitRootInfoConfig)) {
public:
    explicit HijackedFuncOfHcclCommInitRootInfoConfig();
    ~HijackedFuncOfHcclCommInitRootInfoConfig() override = default;
    void Pre(uint32_t nRanks, const HcclRootInfo *rootInfo, uint32_t rank, const HcclCommConfig *config, HcclComm *comm)
        override;
    HcclResult Post(HcclResult ret) override;

private:
    HcclComm *comm_{nullptr};
};

#endif // __HCCL_HIJACKED_FUNC_H__
