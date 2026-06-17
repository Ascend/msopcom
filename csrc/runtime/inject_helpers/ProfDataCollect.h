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


#ifndef __PROF_DATA_COLLECT_H__
#define __PROF_DATA_COLLECT_H__

#include <memory>
#include <thread>
#include <functional>
#include "runtime.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "LaunchContext.h"
#include "acl_rt_impl/AscendclImplOrigin.h"
#include "include/opprof/DbiDefs.h"
/*
 * 性能数据采集，包括：
 * 1. 包括数据性能采集的所有方法，
 * 2. ProfDataCollect提供具有采集功能的公共接口.
 * 3. 目前提供camodel仿真采集和上板采集，分别对应类DataCollectWithSimulator和DataCollectInDevice.
 *
 * 限制：
 * 1. 仅用于性能数据采集功能。
 *
 */
struct RangeReplayConfig {
    bool flag;           // true is in mstx range
    uint32_t count;      // mstx range count
    rtStream_t stream;   // stream of first operator in range
    std::vector<std::string> outputs;  // range replay kernel output paths
};
class DataCollect;
class ProfDataCollect {
public:
    explicit ProfDataCollect(const LaunchContextSP &ctx = nullptr, bool isInitOutput = true);
    static bool SaveObject(const void *hdl);
    static bool SaveObject(const RegisterContextSP &ctx);
    void ProfInit(const void *hdl, const void *stubFunc = nullptr, bool type = true) const;
    bool ProfData() const; // simulator
    bool ProfData(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc); // device
    bool InstrProfData(rtStream_t stream, const std::function<bool(void)> &kernelLaunchFunc);
    void GenBBcountFile(uint64_t regId, uint64_t memSize, uint8_t *memInfo) const;
    void GenDBIData(uint64_t memSize, uint8_t *memInfo) const;
    void GenRecordData(uint64_t memSize, uint8_t *memInfo, const std::string &recordName) const;
    void PostProcess() const; // device
    bool IsNeedProf() const;
    bool IsBBCountNeedGen();
    bool IsMemoryChartNeedGen();
    bool IsNeedRunOriginLaunch();
    bool IsNeedDumpContext();
    bool IsOperandRecordNeedGen();
    bool IsPCSamplingNeedGen();
    bool IsTimelineNeedGen(ProfDBIType &type);
    bool IsWarpTimelineNeedGen();
    bool RangeReplay(const rtStream_t &stream, const aclmdlRI &modelRI);
    static std::string GetAicoreOutputPath(int32_t device);
    static uint32_t GetDeviceReplayCount(int32_t device);
    static RangeReplayConfig GetThreadRangeConfigMap(std::thread::id threadId);
    static void ResetRangeConfig(std::thread::id threadId);
private:
    void TerminateInAdvance() const;
    std::shared_ptr<DataCollect> dataCollect_;
};
#endif // __PROF_DATA_COLLECT_H__
