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


#include <elf.h>
#include <random>
#include <gtest/gtest.h>
#include <map>
#include <string>

#include "core/DomainSocket.h"
#include "core/FuncSelector.h"
#include "runtime.h"
#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/KernelContext.h"
#include "runtime/inject_helpers/LocalDevice.h"
#define private public
#include "runtime/inject_helpers/MemoryDataCollect.h"
#undef private
#include "runtime/inject_helpers/ArgsManager.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "runtime/RuntimeOrigin.h"
#include "stub/FakeElf.h"
#include "utils/Protocol.h"
#include "utils/Serialize.h"

constexpr uint64_t ADDR0 = 0x12;
constexpr uint64_t ADDR1 = 0x34;
constexpr uint64_t ADDR2 = 0x56;
constexpr uint64_t ADDR3 = 0x78;
constexpr uint64_t SIZE0 = 12;
constexpr uint64_t SIZE1 = 34;
constexpr uint64_t SIZE2 = 56;
constexpr uint64_t SIZE3 = 78;

static std::string g_received;

namespace {

uint64_t RandNum(uint64_t min, uint64_t max)
{
    static std::mt19937_64 mersenneEngine(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist(min, max);
    return dist(mersenneEngine);
}

static int g_testMc2DevId = RandNum(0, 3);

static int StubNotify(LocalProcess*, std::string const &msg)
{
    g_received.append(msg);
    return g_received.size();
}

rtError_t RtMemcpyStubFunc(void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind)
{
    std::copy_n(static_cast<const uint8_t*>(src), cnt, static_cast<uint8_t*>(dst));
    return RT_ERROR_NONE;
}

aclError MemcpyStub(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind)
{
    std::copy_n(static_cast<const uint8_t*>(src), count, static_cast<uint8_t*>(dst));
    return ACL_ERROR_NONE;
}

aclError GetDeviceOriginStub(int32_t *devId)
{
    *devId = g_testMc2DevId;
    return ACL_ERROR_NONE;
}

RTS_API rtError_t RtMallocHostStubFunc(void **hostPtr, uint64_t size, uint16_t moduleId)
{
    *hostPtr = malloc(size);
    return RT_ERROR_NONE;
}

rtError_t RtFreeHostStubFunc(void *hostPtr)
{
    free(hostPtr);
    return RT_ERROR_NONE;
}

inline HostMemRecord CreateHostMemRecord(MemOpType type, MemInfoSrc memInfoSrc)
{
    HostMemRecord record{};
    record.type = type;
    record.infoSrc = memInfoSrc;
    record.dstAddr = ADDR0;
    record.memSize = SIZE0;
    return record;
}

inline KernelContext::OpMemInfo CreateOpMemInfo()
{
    KernelContext::OpMemInfo opMemInfo;
    opMemInfo.inputParamsAddrInfos.push_back({0x00, SIZE0});
    opMemInfo.inputParamsAddrInfos.push_back({0x00, SIZE1});
    opMemInfo.inputNum = 0;
    opMemInfo.skipNum = 0;
    return opMemInfo;
}

inline OpMemInfo CreateOpMemInfoFunc()
{
    OpMemInfo opMemInfo;
    opMemInfo.inputParamsAddrInfos.push_back({0x00, SIZE0});
    opMemInfo.inputParamsAddrInfos.push_back({0x00, SIZE1});
    opMemInfo.inputNum = 3;
    opMemInfo.skipNum = 0;
    opMemInfo.secondPtrAddrInfos[1] = {};
    return opMemInfo;
}

inline std::vector<uint64_t> CreateSecondPtrInfos()
{
    std::vector<uint64_t> infos;
    uint64_t ptrOffset = 64;
    infos.push_back(ptrOffset);
    uint32_t dim1 = 3;
    uint32_t cnt1 = 1;
    uint64_t dimCntVal1 = (static_cast<uint64_t>(cnt1) << 32) | static_cast<uint64_t>(dim1);
    infos.push_back(dimCntVal1);
    // push 一级指针dim
    for (size_t i = 0; i < dim1; ++i) {
        infos.push_back(100 * (i + 1));
    }
    uint32_t dim2 = 2;
    uint32_t cnt2 = 1;
    uint64_t dimCntVal2 = (static_cast<uint64_t>(cnt2) << 32) | static_cast<uint64_t>(dim2);
    infos.push_back(dimCntVal2);
    // push 一级指针dim
    for (size_t i = 0; i < dim2; ++i) {
        infos.push_back(50 * (i + 1));
    }
    // push 一级指针地址
    infos.insert(infos.end(), {0x100, 0x200});
    return infos;
}

inline std::vector<rtHostInputInfo_t> CreateHostInputInfos()
{
    return {
        {0, 24},
    };
}

inline std::vector<HostMemoryInfo> SetToVec(std::set<HostMemoryInfo> &setInfo)
{
    std::vector<HostMemoryInfo> vecInfo;
    for (const auto &info : setInfo) {
        vecInfo.push_back(info);
    }
    return vecInfo;
}

} // namespace [Dummy]

TEST(MemoryDataCollect, report_malloc_expect_recieve_correct_message)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(aclrtGetDeviceImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&LocalDevice::Notify).stubs().will(invoke(StubNotify));

    g_received.clear();
    ReportMalloc(ADDR0, SIZE0, MemInfoSrc::EXTRA);
    GlobalMockObject::verify();

    PacketHead head = { PacketType::MEMORY_RECORD };
    HostMemRecord record = CreateHostMemRecord(MemOpType::MALLOC, MemInfoSrc::EXTRA);
    ASSERT_EQ(g_received, Serialize(head, record));
}

TEST(MemoryDataCollect, report_free_expect_recieve_correct_message)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(aclrtGetDeviceImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&LocalDevice::Notify).stubs().will(invoke(StubNotify));

    g_received.clear();
    ReportFree(ADDR0, MemInfoSrc::EXTRA);
    GlobalMockObject::verify();

    PacketHead head = { PacketType::MEMORY_RECORD };
    HostMemRecord record = CreateHostMemRecord(MemOpType::FREE, MemInfoSrc::EXTRA);
    // free should never care about memSize
    record.memSize = 0;
    ASSERT_EQ(g_received, Serialize(head, record));
}

TEST(MemoryDataCollect, report_memset_expect_recieve_correct_message)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(aclrtGetDeviceImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&LocalDevice::Notify).stubs().will(invoke(StubNotify));

    g_received.clear();
    ReportMemset(ADDR0, SIZE0, MemInfoSrc::EXTRA);
    GlobalMockObject::verify();

    PacketHead head = { PacketType::MEMORY_RECORD };
    HostMemRecord record = CreateHostMemRecord(MemOpType::STORE, MemInfoSrc::EXTRA);
    ASSERT_EQ(g_received, Serialize(head, record));
}

TEST(MemoryDataCollect, report_op_malloc_info_with_null_args_info_expect_do_nothing)
{
    KernelContext::OpMemInfo opMemInfo;
    ReportOpMallocInfo(nullptr, opMemInfo);
    ASSERT_TRUE(opMemInfo.inputParamsAddrInfos.empty());

    rtArgsEx_t argsInfo {};
    ReportOpMallocInfo(&argsInfo, opMemInfo);
    ASSERT_TRUE(opMemInfo.inputParamsAddrInfos.empty());
}

TEST(MemoryDataCollect, report_op_malloc_info_with_null_launch_args_info_expect_do_nothing)
{
    OpMemInfo opMemInfo;
    AclrtLaunchArgsInfo launchArgs{};
    ReportOpMallocInfo(launchArgs, opMemInfo);
    ASSERT_TRUE(opMemInfo.inputParamsAddrInfos.empty());
}

TEST(MemoryDataCollect, report_op_malloc_info_with_regular_tensor_expect_get_correct_mem_info)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(ReportMalloc).stubs();
    MOCKER(ReportMemset).stubs();

    // create regular tensor memInfo size
    KernelContext::OpMemInfo opMemInfo = CreateOpMemInfo();
    // create tensor addr
    rtArgsEx_t argsInfo {};
    std::string args = Serialize(ADDR0, ADDR1);
    argsInfo.args = const_cast<char *>(args.data());
    argsInfo.argsSize = args.size();

    ReportOpMallocInfo(&argsInfo, opMemInfo);
    GlobalMockObject::verify();

    ASSERT_EQ(opMemInfo.uniqueAddrInfos.size(), 2UL);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[0].addr, ADDR0);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[0].length, SIZE0);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[1].addr, ADDR1);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[1].length, SIZE1);
}

TEST(MemoryDataCollect, report_op_malloc_info_with_launch_args_tiling_expect_get_correct_mem_info)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(ReportMalloc).stubs();
    MOCKER(ReportMemset).stubs();

    // create regular tensor memInfo size with tiling
    OpMemInfo opMemInfo = CreateOpMemInfoFunc();
    // create tensor addr
    AclrtLaunchArgsInfo launchArgs{};
    std::string args = Serialize(ADDR0, ADDR1, ADDR2, ADDR0, ADDR0, ADDR0, ADDR0);
    launchArgs.hostArgs = const_cast<char *>(args.data());
    launchArgs.argsSize = args.size() + SIZE2;
    launchArgs.placeHolderNum = 2;
    launchArgs.placeHolderArray = reinterpret_cast<aclrtPlaceHolderInfo *>(const_cast<char *>(args.data()) + 24);

    ReportOpMallocInfo(launchArgs, opMemInfo);
    GlobalMockObject::verify();

    ASSERT_EQ(opMemInfo.uniqueAddrInfos.size(), 3UL);
}

TEST(MemoryDataCollect, report_op_malloc_info_with_tiling_expect_get_correct_mem_info)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(ReportMalloc).stubs();
    MOCKER(ReportMemset).stubs();

    // create regular tensor memInfo size with tiling
    KernelContext::OpMemInfo opMemInfo = CreateOpMemInfo();
    // create tensor addr
    rtArgsEx_t argsInfo {};
    std::string args = Serialize(ADDR0, ADDR1, ADDR2);
    argsInfo.args = const_cast<char *>(args.data());
    argsInfo.argsSize = args.size() + SIZE2;
    argsInfo.hasTiling = 1;
    argsInfo.tilingAddrOffset = sizeof(uint64_t) + sizeof(uint64_t);
    argsInfo.tilingDataOffset = args.size();

    ReportOpMallocInfo(&argsInfo, opMemInfo);
    GlobalMockObject::verify();

    ASSERT_EQ(opMemInfo.uniqueAddrInfos.size(), 3UL);
    // tiling 信息保存时被插入到最前面
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[0].addr, ADDR2);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[0].length, (SIZE2 + 31U) / 32U * 32U); // tiling size需要加31向上对齐到32字节
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[1].addr, ADDR0);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[1].length, SIZE0);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[2].addr, ADDR1);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[2].length, SIZE1);
}

TEST(MemoryDataCollect, report_op_malloc_info_with_empty_host_input_info_expect_get_zero_mem_size)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(ReportMalloc).stubs();
    MOCKER(ReportMemset).stubs();

    // create tensor memInfo with second ptr info map
    KernelContext::OpMemInfo opMemInfo{};
    opMemInfo.inputParamsAddrInfos.resize(2);
    opMemInfo.secondPtrAddrInfos.insert({0, {}});
    opMemInfo.secondPtrAddrInfos.insert({1, {}});
    // create tensor addr
    rtArgsEx_t argsInfo {};
    std::string args = Serialize(ADDR0, ADDR1);
    argsInfo.args = const_cast<char *>(args.data());
    argsInfo.argsSize = args.size();

    ReportOpMallocInfo(&argsInfo, opMemInfo);
    GlobalMockObject::verify();

    ASSERT_EQ(opMemInfo.uniqueAddrInfos.size(), 0UL);
}

TEST(MemoryDataCollect, report_op_malloc_info_with_shape_info_expect_get_correct_mem_info)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(ReportMalloc).stubs();
    MOCKER(ReportMemset).stubs();

    // create tensor memInfo with second ptr info map
    KernelContext::OpMemInfo opMemInfo = CreateOpMemInfo();
    // 插入两个二级指针，对应的index为0和1
    SecondPtrInfo secondPtrInfo{};
    secondPtrInfo.dtypeBytes = 2;
    opMemInfo.secondPtrAddrInfos.insert({0, secondPtrInfo});
    // create tensor addr
    rtArgsEx_t argsInfo {};
    // 第一个二级指针地址为1UL，第二个二级指针地址为
    std::vector<uint64_t> args{1UL, ADDR0, ADDR1};
    auto secondPtrInfos = CreateSecondPtrInfos();
    args.insert(args.end(), secondPtrInfos.begin(), secondPtrInfos.end());
    argsInfo.args = args.data();
    argsInfo.argsSize = args.size();
    argsInfo.hostInputInfoNum = 1;
    // create host input infos
    auto hostInputInfos = CreateHostInputInfos();
    argsInfo.hostInputInfoPtr = const_cast<rtHostInputInfo_t*>(hostInputInfos.data());

    ReportOpMallocInfo(&argsInfo, opMemInfo);
    GlobalMockObject::verify();

    // 排布顺序为：二级指针/一级指针/二级指针中的一级指针/级指针中的一级指针
    ASSERT_EQ(opMemInfo.uniqueAddrInfos.size(), 4UL);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[0].addr, 1UL);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[0].length, SIZE0);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[1].addr, ADDR0);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[1].length, SIZE1);
    /// 二级指针中的一级指针信息
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[2].addr, 0x100);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[2].length, 12000000);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[3].addr, 0x200);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[3].length, 10000);
}

TEST(MemoryDataCollect, report_op_free_info_with_expect_get_correct_mem_info)
{
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(&DomainSocketClient::Connect).stubs().will(returnValue(true));
    MOCKER(aclrtGetDeviceImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&LocalDevice::Notify).stubs().will(invoke(StubNotify));

    // create tensor memInfo with uniqueAddrInfos
    KernelContext::OpMemInfo opMemInfo;
    opMemInfo.uniqueAddrInfos.push_back({ADDR0, SIZE0, MemInfoSrc::EXTRA});
    opMemInfo.uniqueAddrInfos.push_back({ADDR1, SIZE1, MemInfoSrc::EXTRA});

    // report op free info
    g_received.clear();
    ReportOpFreeInfo(opMemInfo);
    std::string opFreeMsg = g_received;

    // report free manually
    g_received.clear();
    ReportFree(ADDR0, MemInfoSrc::EXTRA);
    ReportFree(ADDR1, MemInfoSrc::EXTRA);
    std::string manualFreeMsg = g_received;

    GlobalMockObject::verify();

    ASSERT_EQ(opFreeMsg, manualFreeMsg);
}

TEST(MemoryDataCollect, get_section_headers_from_dev_binary_with_null_data_expect_return_false)
{
    rtDevBinary_t binary {};
    std::map<std::string, Elf64_Shdr> headers;
    ASSERT_FALSE(GetSectionHeaders(binary, headers));
}

TEST(MemoryDataCollect, get_section_headers_from_dev_binary_with_invalid_elf_expect_return_false)
{
    rtDevBinary_t binary {};
    Buffer elf = {'1', '2', '3'};
    binary.data = elf.data();
    std::map<std::string, Elf64_Shdr> headers;
    ASSERT_FALSE(GetSectionHeaders(binary, headers));
}

TEST(MemoryDataCollect, get_section_headers_from_valid_elf_expect_return_true_and_get_correct_headers)
{
    rtDevBinary_t binary {};
    Buffer elf = CreateValidElf();
    binary.data = elf.data();
    binary.length = elf.size();
    std::map<std::string, Elf64_Shdr> headers;
    ASSERT_TRUE(GetSectionHeaders(binary, headers));
    ASSERT_NE(headers.find(".shstrtab"), headers.cend());
    ASSERT_NE(headers.find(".dummy"), headers.cend());
}

TEST(MemoryDataCollect, get_alloc_section_headers_expect_return_correct_headers)
{
    std::map<std::string, Elf64_Shdr> headers;
    Elf64_Shdr header {};
    headers.emplace(".nonalloc", header);
    header.sh_flags = SHF_ALLOC;
    header.sh_size = 100;
    headers.emplace(".alloc", header);
    auto allocHeaders = GetAllocSectionHeaders(headers);
    ASSERT_EQ(allocHeaders.size(), 1UL);
    ASSERT_TRUE(static_cast<bool>(allocHeaders[0].sh_flags & SHF_ALLOC));
}

TEST(MemoryDataCollect, report_sections_malloc_with_invalid_pcstart_addr_expect_report_nothing)
{
    ReportSectionsMalloc(0x00, {});
}

TEST(MemoryDataCollect, report_sections_malloc_with_valid_pcstart_addr_expect_report_correct)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));

    Elf64_Shdr header {};
    header.sh_flags = SHF_ALLOC;
    std::vector<Elf64_Shdr> allocHeaders(2, header);
    ReportSectionsMalloc(0x10, allocHeaders);
    GlobalMockObject::verify();
}

TEST(MemoryDataCollect, report_sections_free_with_invalid_pcstart_addr_expect_report_nothing)
{
    ReportSectionsFree(0x00, {});
}

TEST(MemoryDataCollect, report_sections_free_with_valid_pcstart_addr_expect_report_correct)
{
    MOCKER(&LocalProcess::Notify).stubs().will(returnValue(0));

    Elf64_Shdr header {};
    header.sh_flags = SHF_ALLOC;
    std::vector<Elf64_Shdr> allocHeaders(2, header);
    ReportSectionsFree(0x10, allocHeaders);
    GlobalMockObject::verify();
}

inline HcclCombinOpParam CreateHcclCombineOpParams(uint32_t rankId, uint32_t rankNum)
{
    HcclCombinOpParam opParam{};
    opParam.rankId = rankId;
    opParam.rankNum = rankNum;
    opParam.winSize = 200;
    for (size_t i = 0; i < opParam.rankNum; ++i) {
        opParam.windowsIn[i] = 0x100 * (i + 1);
        opParam.windowsOut[i] = 0x3100 * (i + 1);
    }
    return opParam;
}

TEST(MemoryDataCollect, report_mc2_op_malloc_with_adump_info_expect_get_correct_addr_info)
{
    ArgsManager::Instance().Clear();
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(ReportMalloc).stubs();
    MOCKER(ReportMemset).stubs();
    MOCKER(&KernelContext::GetMc2CtxFlag).stubs().will(returnValue(true));
    MOCKER(&rtMemcpyOrigin).stubs().will(invoke(RtMemcpyStubFunc));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(invoke(MemcpyStub));
    MOCKER(&aclrtGetDeviceImplOrigin).stubs().will(invoke(GetDeviceOriginStub));
    MOCKER(&rtMallocHostOrigin).stubs().will(invoke(RtMallocHostStubFunc));
    MOCKER(&rtFreeHostOrigin).stubs().will(invoke(RtFreeHostStubFunc));
    KernelContext::OpMemInfo opMemInfo = CreateOpMemInfo();
    // create tensor addr
    rtArgsEx_t argsInfo {};
    uint32_t rankNum = RandNum(4, 8);
    uint32_t rankId = g_testMc2DevId;
    HcclCombinOpParam opParam = CreateHcclCombineOpParams(rankId, rankNum);
    uint64_t mc2ContextAddr = reinterpret_cast<uint64_t>(&opParam);
    std::vector<uint64_t> args{mc2ContextAddr, 0x256};

    argsInfo.args = args.data();
    argsInfo.argsSize = args.size();

    ReportOpMallocInfo(&argsInfo, opMemInfo);

    /// 排布顺序：windowsIn/windowsOut/.../二级指针/一级指针
    size_t windowsNum = 2;
    ASSERT_EQ(opMemInfo.uniqueAddrInfos.size(), 2 + 2 * rankNum);
    size_t baseIdx = 0;
    for (size_t i = 0; i < rankNum; ++i) {
        baseIdx = i * windowsNum;
        ASSERT_EQ(opMemInfo.uniqueAddrInfos[baseIdx].addr, opParam.windowsIn[i]);
        ASSERT_EQ(opMemInfo.uniqueAddrInfos[baseIdx].length, 200);
        ASSERT_EQ(opMemInfo.uniqueAddrInfos[baseIdx].memInfoSrc, MemInfoSrc::EXTRA);
        ASSERT_EQ(opMemInfo.uniqueAddrInfos[baseIdx + 1].addr, opParam.windowsOut[i]);
        ASSERT_EQ(opMemInfo.uniqueAddrInfos[baseIdx + 1].length, 200);
        ASSERT_EQ(opMemInfo.uniqueAddrInfos[baseIdx].memInfoSrc, MemInfoSrc::EXTRA);
    }
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[rankNum * windowsNum].addr, mc2ContextAddr);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[rankNum * windowsNum].length, SIZE0);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[rankNum * windowsNum + 1].addr, 0x256);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[rankNum * windowsNum + 1].length, SIZE1);
    GlobalMockObject::verify();
}

TEST(MemoryDataCollect, report_mc2_op_malloc_with_no_adump_info_expect_get_correct_addr_info)
{
    ArgsManager::Instance().Clear();
    MOCKER(IsSanitizer).stubs().will(returnValue(true));
    MOCKER(ReportMalloc).stubs();
    MOCKER(ReportMemset).stubs();
    MOCKER(&KernelContext::GetMc2CtxFlag).stubs().will(returnValue(true));
    ArgsManager::Instance().SetThroughAdumpFlag(false);
    MOCKER(&rtMemcpyOrigin).stubs().will(invoke(RtMemcpyStubFunc));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(invoke(MemcpyStub));
    MOCKER(&aclrtGetDeviceImplOrigin).stubs().will(invoke(GetDeviceOriginStub));
    MOCKER(&rtMallocHostOrigin).stubs().will(invoke(RtMallocHostStubFunc));
    MOCKER(&rtFreeHostOrigin).stubs().will(invoke(RtFreeHostStubFunc));
    KernelContext::OpMemInfo opMemInfo = CreateOpMemInfo();
    // create tensor addr
    rtArgsEx_t argsInfo {};
    uint32_t rankNum = RandNum(4, 8);
    uint32_t rankId = g_testMc2DevId;
    HcclCombinOpParam opParam = CreateHcclCombineOpParams(rankId, rankNum);
    uint64_t mc2ContextAddr = reinterpret_cast<uint64_t>(&opParam);
    std::vector<uint64_t> args{mc2ContextAddr, 0x256};

    argsInfo.args = args.data();
    argsInfo.argsSize = args.size();

    ReportOpMallocInfo(&argsInfo, opMemInfo);

    /// 排布顺序：windowsIn/windowsOut/.../一级指针
    size_t windowsNum = 2;
    ASSERT_EQ(opMemInfo.uniqueAddrInfos.size(), 1 + windowsNum * rankNum - windowsNum);
    for (size_t i = 0; i < rankNum; ++i) {
        size_t baseIdx = 0;
        if (i < rankId) {
            baseIdx = i * windowsNum;
        } else if (i == rankId) {
            continue;
        } else {
            baseIdx = i * windowsNum - windowsNum;
        }
        ASSERT_EQ(opMemInfo.uniqueAddrInfos[baseIdx].addr, opParam.windowsIn[i]);
        ASSERT_EQ(opMemInfo.uniqueAddrInfos[baseIdx].length, 200);
        ASSERT_EQ(opMemInfo.uniqueAddrInfos[baseIdx].memInfoSrc, MemInfoSrc::BYPASS);
        ASSERT_EQ(opMemInfo.uniqueAddrInfos[baseIdx + 1].addr, opParam.windowsOut[i]);
        ASSERT_EQ(opMemInfo.uniqueAddrInfos[baseIdx + 1].length, 200);
        ASSERT_EQ(opMemInfo.uniqueAddrInfos[baseIdx].memInfoSrc, MemInfoSrc::BYPASS);
    }
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[windowsNum * rankNum - windowsNum].addr, 0x256);
    ASSERT_EQ(opMemInfo.uniqueAddrInfos[windowsNum * rankNum - windowsNum].length, SIZE1);
    GlobalMockObject::verify();

}

TEST(MemoryDataCollect, memory_manage_process_mstx_heap_mem_expect_success)
{
    MemoryManage::Instance().Reset();
    MemoryManage::AddrsSet addrsSet;
    addrsSet.insert({0x100, 100});
    addrsSet.insert({0x200, 100});
    KernelContext::AddrInfo addrInfo1{0x164, 156, MemInfoSrc::MSTX_HEAP};
    MemoryManage::Instance().ProcessMstxHeapMem(addrsSet, addrInfo1);
    ASSERT_EQ(addrsSet.size(), 2);
    KernelContext::AddrInfo addrInfo2{0x200, 100, MemInfoSrc::MSTX_HEAP};
    MemoryManage::Instance().ProcessMstxHeapMem(addrsSet, addrInfo2);
    ASSERT_EQ(addrsSet.size(), 1);
    KernelContext::AddrInfo addrInfo3{0x120, 50, MemInfoSrc::MSTX_HEAP};
    MemoryManage::Instance().ProcessMstxHeapMem(addrsSet, addrInfo3);
    ASSERT_EQ(addrsSet.size(), 2);
    std::vector<HostMemoryInfo> vecInfo = SetToVec(addrsSet);
    ASSERT_EQ(vecInfo[0].addr, 0x100);
    ASSERT_EQ(vecInfo[0].size, 32);
    ASSERT_EQ(vecInfo[1].addr, 0x120 + 50);
    ASSERT_EQ(vecInfo[1].size, 18);
    MemoryManage::Instance().Reset();
}

TEST(MemoryDataCollect, memory_manage_extract_extra_mem_expect_success)
{
    MemoryManage::Instance().Reset();
    std::set<KernelContext::AddrInfo> addrsSet;
    addrsSet.insert({0x100, 100, MemInfoSrc::RT});
    addrsSet.insert({0x200, 100, MemInfoSrc::EXTRA});
    addrsSet.insert({0x300, 100, MemInfoSrc::HAL});
    addrsSet.insert({0x400, 100, MemInfoSrc::EXTRA});
    addrsSet.insert({0x500, 100, MemInfoSrc::ACL});
    addrsSet.insert({0x600, 100, MemInfoSrc::EXTRA});
    addrsSet.insert({0x700, 100, MemInfoSrc::MSTX_HEAP});
    addrsSet.insert({0x800, 100, MemInfoSrc::BYPASS});
    addrsSet.insert({0x900, 100, MemInfoSrc::MSTX_REGION});
    addrsSet.insert({0x1000, 100, MemInfoSrc::BYPASS});
    addrsSet.insert({0x1100, 100, MemInfoSrc::MANUAL});
    MemoryManage::AddrsVec extrAddrs = MemoryManage::Instance().ExtractValidMems(addrsSet);
    ASSERT_EQ(extrAddrs.size(), 6);
    ASSERT_EQ(extrAddrs[0].addr, 0x200);
    ASSERT_EQ(extrAddrs[1].addr, 0x400);
    ASSERT_EQ(extrAddrs[2].addr, 0x600);
    ASSERT_EQ(extrAddrs[3].addr, 0x800);
    ASSERT_EQ(extrAddrs[4].addr, 0x1000);
    ASSERT_EQ(extrAddrs[5].addr, 0x1100);
    MemoryManage::Instance().Reset();
}

TEST(MemoryDataCollect, memory_manage_extract_rt_mem_expect_success)
{
    MemoryManage::Instance().Reset();
    std::set<KernelContext::AddrInfo> addrsSet;
    addrsSet.insert({0x100, 100, MemInfoSrc::RT});
    addrsSet.insert({0x200, 100, MemInfoSrc::ACL});
    addrsSet.insert({0x200, 100, MemInfoSrc::MSTX_HEAP});
    addrsSet.insert({0x300, 100, MemInfoSrc::RT});
    addrsSet.insert({0x400, 100, MemInfoSrc::BYPASS});
    addrsSet.insert({0x500, 100, MemInfoSrc::MSTX_REGION});
    addrsSet.insert({0x600, 100, MemInfoSrc::BYPASS});
    addrsSet.insert({0x700, 100, MemInfoSrc::MANUAL});
    MemoryManage::AddrsVec extrAddrs = MemoryManage::Instance().ExtractValidMems(addrsSet);
    ASSERT_EQ(extrAddrs.size(), 6);
    ASSERT_EQ(extrAddrs[0].addr, 0x400);
    ASSERT_EQ(extrAddrs[1].addr, 0x500);
    ASSERT_EQ(extrAddrs[2].addr, 0x600);
    ASSERT_EQ(extrAddrs[3].addr, 0x700);
    ASSERT_EQ(extrAddrs[4].addr, 0x100);
    ASSERT_EQ(extrAddrs[5].addr, 0x300);
    MemoryManage::Instance().Reset();
}

TEST(MemoryDataCollect, memory_manage_merge_rt_mem_expect_success)
{
    MemoryManage::Instance().Reset();
    MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(0x400, MemInfoSrc::MSTX_REGION, 100);
    MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(0x100, MemInfoSrc::RT, 100);
    MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(0x200, MemInfoSrc::ACL, 100);
    MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(0x110, MemInfoSrc::RT, 100);
    MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(0x300, MemInfoSrc::BYPASS, 256);
    MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(0x600, MemInfoSrc::BYPASS, 100);
    MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(0x200, MemInfoSrc::MSTX_HEAP, 100);
    MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(0x600, MemInfoSrc::RT, 100);
    MemoryManage::Instance().CacheMemory<MemoryOpType::MALLOC>(0x500, MemInfoSrc::MANUAL, 256);
    MemoryManage::AddrsVec extrAddrs = MemoryManage::Instance().MergeHostMems();
    ASSERT_EQ(extrAddrs.size(), 9);
    ASSERT_EQ(extrAddrs[0].addr, 0x100);
    ASSERT_EQ(extrAddrs[0].size, 116);
    ASSERT_EQ(extrAddrs[1].addr, 0x300);
    ASSERT_EQ(extrAddrs[1].size, 356);
    ASSERT_EQ(extrAddrs[2].addr, 0x500);
    ASSERT_EQ(extrAddrs[2].size, 356);
    for (size_t i = 3; i < extrAddrs.size(); ++i) {
        ASSERT_EQ(extrAddrs[i].addr, 0);
        ASSERT_EQ(extrAddrs[i].size, 0);
    }
    MemoryManage::Instance().Reset();
}
