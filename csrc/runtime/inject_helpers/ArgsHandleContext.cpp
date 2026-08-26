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


#include "ArgsHandleContext.h"

#include <memory>
#include <vector>

#include "acl_rt_impl/AscendclImplOrigin.h"
#include "runtime/inject_helpers/KernelContext.h"
#include "utils/InjectLogger.h"

using namespace std;
namespace {
void GetFlattenedParams(std::vector<ArgsHandleContext::ParamData> &flattenParams,
                        const std::vector<ArgsHandleContext::ParamData> &params)
{
    for (std::size_t i = 0; i < params.size() ; i++) {
        ArgsHandleContext::ParamData param;
        if (params[i].type != ArgsHandleContext::ParamType::NORMAL) {
            param = params[i];
            flattenParams.emplace_back(param);
            continue;
        }
        auto paramLen = params[i].param.size() / sizeof(uintptr_t);
        for (std::size_t j = 0; j < paramLen; ++j) {
            param.param.assign(params[i].param.begin() + (j * sizeof(uintptr_t)),
                               params[i].param.begin() + ((j + 1) * sizeof(uintptr_t)));
            param.type = ArgsHandleContext::ParamType::NORMAL;
            flattenParams.emplace_back(param);
        }
    }
}
}
bool ArgsHandleContext::ExpandArgs(void *param, size_t paramSize, uint32_t &paramOffset)
{
    aclrtParamHandle paramHandle{};
    this->CacheArgsAppendOp(param, paramSize, paramHandle);

    return true;
}

bool ArgsHandleContext::Save(const std::string &outputPath, DumperContext &config, OpMemInfo &memInfo, bool isSink)
{
    (void)isSink;
    // 当前参数都有缓存，暂时不再需要区分图下沉
    std::vector<uint8_t> tilingData;
    return UpdateNormalTaskArgsAddr(memInfo) &&
           DumpInputData(outputPath, memInfo.inputParamsAddrInfos, config) &&
           // 如果有 tiling data，tiling data dump 需要成功
           (!GetTilingData(tilingData) || DumpTilingData(outputPath, tilingData, config));
}

ArgsContextSP ArgsHandleContext::Clone(void) const
{
    return std::make_shared<ArgsHandleContext>(*this);
}

bool ArgsHandleContext::GetTilingData(std::vector<uint8_t> &data) const
{
    // 当前只能猜测第一个placeholder对应的数据是tilingData
    // 并且要finalize后才能拿到
    if (!finalized_) {
        return false;
    }
    for (auto &p: params_) {
        if (p.type == ParamType::PLACEHOLDER) {
            data = p.param;
            return true;
        }
    }
    return false;
}

void ArgsHandleContext::CacheArgsAppendOp(void *param, size_t paramSize, aclrtParamHandle paramHandle)
{
    if (paramSize > MAX_SINGLE_PARAM_SIZE) {
        WARN_LOG("ParamSize exceeds limit: %zu > %zu", paramSize, MAX_SINGLE_PARAM_SIZE);
        return;
    }
    ArgsHandleContext::ParamData paramData{};
    paramData.param = vector<uint8_t>(static_cast<uint8_t*>(param),
                                      static_cast<uint8_t*>(param) + paramSize);
    paramData.type = ParamType::NORMAL;
    paramData.paramHandle = paramHandle;

    DEBUG_LOG("paramHandle: %p, paramSize: %zu, type: %u, pos: %lu", paramData.paramHandle,
              paramData.param.size(), static_cast<uint32_t>(paramData.type),
              params_.size());

    params_.push_back(std::move(paramData));
}

void ArgsHandleContext::CacheArgsAppendPlaceholderOp(aclrtParamHandle paramHandle)
{
    ArgsHandleContext::ParamData paramData{};
    paramData.paramHandle = paramHandle;
    paramData.type = ParamType::PLACEHOLDER;
    params_.push_back(std::move(paramData));
}

void ArgsHandleContext::CacheArgsParaUpdateOp(aclrtParamHandle paramHandle, void *param, size_t paramSize)
{
    for (auto &p: params_) {
        if (p.paramHandle == paramHandle) {
            p.param = vector<uint8_t>(static_cast<uint8_t*>(param),
                                      static_cast<uint8_t*>(param) + paramSize);
            return;
        }
    }
    DEBUG_LOG("Can not find target param handle, update para failed.");
}

aclrtArgsHandle ArgsHandleContext::GenerateArgsHandle()
{
    // 改用 aclrtKernelArgsInitImplOrigin 重建句柄。runtime 对该接口固定
    // maxUserParamNum=MAX_PARAM_CNT(128)、bufferSize=MAX_ARGS_BUFF_SIZE(64KB)，
    // 不依赖 kernel 的 DFX_ARG_INFO 元数据，DSL 内核（AscendNPU-IR 生成、缺元数据）
    // 亦能正常追加参数。
    aclrtArgsHandle argsHandle = nullptr;
    aclError ret = aclrtKernelArgsInitImplOrigin(funcHandle_, &argsHandle);
    if (ret != ACL_ERROR_NONE) {
        WARN_LOG("Init expanded args handle failed.");
        return nullptr;
    }

    DEBUG_LOG("GenerateArgsHandle funcHandle=%p params_=%zu", funcHandle_, params_.size());

    if (!this->ReplayArgs(argsHandle)) {
        return nullptr;
    }

    // finalize args handle
    ret = aclrtKernelArgsFinalizeImplOrigin(argsHandle);
    if (ret != ACL_ERROR_NONE) {
        WARN_LOG("Finalize kernel args failed.");
        return nullptr;
    }

    return argsHandle;
}

void ArgsHandleContext::CacheArgsGetPlaceholderBufferOp(aclrtParamHandle paramHandle, size_t dataSize, void *buffer)
{
    for (auto &p: params_) {
        if (p.paramHandle == paramHandle) {
            p.srcBuffer = buffer;
            p.param.resize(dataSize);
            return;
        }
    }
    DEBUG_LOG("Can not find target param handle when cache placeholder buffer.");
}

void ArgsHandleContext::Finalize()
{
    for (auto &p: params_) {
        if (p.type == ParamType::PLACEHOLDER) {
            p.param = vector<uint8_t>(static_cast<uint8_t*>(p.srcBuffer),
                                      static_cast<uint8_t*>(p.srcBuffer) + p.param.size());
        }
    }
    finalized_ = true;
}

size_t ArgsHandleContext::CalcUserArgsSize(void) const
{
    size_t userArgsSize{};
    for (auto const &p : params_) {
        userArgsSize += p.param.size();
    }
    return userArgsSize;
}

bool ArgsHandleContext::ReplayArgs(aclrtArgsHandle argsHandle) {
    aclError ret{};
    std::vector<aclrtParamHandle> paramHandles(params_.size());
    // Replay args append
    for (std::size_t idx = 0; idx < params_.size(); ++idx) {
        auto &param = params_[idx].param;
        if (params_[idx].type == ParamType::NORMAL) {
            // Append normal args directly
            ret = aclrtKernelArgsAppendImplOrigin(argsHandle, param.data(), param.size(), &paramHandles[idx]);
            if (ret != ACL_ERROR_NONE) {
                WARN_LOG("Append normal kernel args failed.");
                return false;
            }
        } else {
            // Append placeholder
            ret = aclrtKernelArgsAppendPlaceHolderImplOrigin(argsHandle, &paramHandles[idx]);
            if (ret != ACL_ERROR_NONE) {
                WARN_LOG("Append kernel args placeholder failed.");
                return false;
            }
        }
    }

    void *buffer{};
    // Replay placeholder buffer init
    for (std::size_t idx = 0; idx < params_.size(); ++idx) {
        if (params_[idx].type != ParamType::PLACEHOLDER) {
            continue;
        }
        auto &param = params_[idx].param;
        ret = aclrtKernelArgsGetPlaceHolderBufferImplOrigin(argsHandle, paramHandles[idx], param.size(), &buffer);
        if (ret != ACL_ERROR_NONE) {
            WARN_LOG("Get placeholder buffer failed.");
            return false;
        }

        std::copy(param.cbegin(), param.cend(), static_cast<uint8_t*>(buffer));
    }
    return true;
}

bool ArgsHandleContext::UpdateNormalTaskArgsAddr(OpMemInfo &memInfo) const
{
    std::vector<ParamData> flattenParams;
    GetFlattenedParams(flattenParams, params_);
    if (flattenParams.size() < memInfo.inputParamsAddrInfos.size()) {
        WARN_LOG("Cached args size smaller than input param size. %zu vs. %zu",
                 flattenParams.size(), memInfo.inputParamsAddrInfos.size());
        return false;
    }
    for (std::size_t i = 0; i < memInfo.inputParamsAddrInfos.size(); ++i) {
        if (flattenParams[i].param.size() != sizeof(uintptr_t)) {
            WARN_LOG("invalid param, pointer param expected");
            return false;
        }
        uint64_t addr = *reinterpret_cast<uint64_t const*>(flattenParams[i].param.data());
        memInfo.inputParamsAddrInfos[i].addr = addr;
    }
    return true;
}
