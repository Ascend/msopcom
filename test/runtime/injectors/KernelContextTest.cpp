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


#include "runtime/inject_helpers/ArgsContext.h"
#define private public
#include "runtime/inject_helpers/KernelContext.h"
#undef private

#include <elf.h>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>

#include <gtest/gtest.h>

#include "acl_rt_impl/AscendclImplOrigin.h"
#include "runtime/RuntimeOrigin.h"
#include "utils/ElfLoader.h"
#include "utils/FileSystem.h"
#include "utils/PipeCall.h"
#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/ArgsContext.h"
#include "runtime/inject_helpers/MemoryDataCollect.h"
#include "runtime/inject_helpers/RegisterContext.h"
#include "runtime/inject_helpers/LaunchManager.h"
#include "runtime/inject_helpers/DeviceContext.h"
#include "runtime/inject_helpers/ArgsManager.h"
#include "core/FuncSelector.h"

using namespace std;
constexpr mode_t EXEC_AUTHORITY = 0740;

namespace {

class KernelContextTest : public testing::Test {
public:
    static void SetUpTestCase() {}

    void SetUp() override
    {
        KernelContext::Instance().Reset();
    }

    void TearDown() override
    {
        KernelContext::Instance().Reset();
        GlobalMockObject::verify();
        ArgsManager::Instance().Clear();
        LaunchManager::Local().Clear();
    }
};

aclError AclrtMallocHostStub(void **hostPtr, size_t size) {
    *hostPtr = malloc(size);
    return ACL_ERROR_NONE;
}

aclError AclrtFreeHostStub(void *hostPtr) {
    free(hostPtr);
    return ACL_ERROR_NONE;
}

rtError_t RtMemcpyStub(void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind)
{
    copy_n(static_cast<const uint8_t*>(src), cnt, static_cast<uint8_t*>(dst));
    return RT_ERROR_NONE;
}

aclError MemcpyStub(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind)
{
    copy_n(static_cast<const uint8_t*>(src), count, static_cast<uint8_t*>(dst));
    return RT_ERROR_NONE;
}

RTS_API rtError_t RtMallocHostStub(void **hostPtr, uint64_t size, uint16_t moduleId)
{
    *hostPtr = malloc(size);
    return RT_ERROR_NONE;
}

rtError_t RtFreeHostStub(void *hostPtr)
{
    free(hostPtr);
    return RT_ERROR_NONE;
}

vector<uint8_t> MockDyanmicAscendcKernelMetaSection()
{
    vector<uint8_t> metaData;
    const static uint32_t DATA[] = {
        0x01000400, 0x03000000, 0x04005c00, 0x00050002,
        0x00000000, 0x00010002, 0xffffffff, 0xffffffff,
        0x00050002, 0x00000000, 0x00010002, 0xffffffff,
        0xffffffff, 0x00050002, 0x00000000, 0x00010003,
        0xffffffff, 0xffffffff, 0x00050001, 0x00000000,
        0x00010004, 0x00050002, 0x00000000, 0x00010007,
        0x00000000, 0x00000010, 0x08000400, 0x100b0000,
    };
    int num = sizeof(DATA);
    const uint8_t *src = reinterpret_cast<const uint8_t*>(DATA);
    for (int i = 0; i < num / sizeof(uint32_t); i++) {
        // 大端序：最高有效字节(MSB)在前
        metaData.push_back(static_cast<uint8_t>((DATA[i] >> 24) & 0xFF)); // 字节0 (MSB)
        metaData.push_back(static_cast<uint8_t>((DATA[i] >> 16) & 0xFF)); // 字节1
        metaData.push_back(static_cast<uint8_t>((DATA[i] >> 8)  & 0xFF));  // 字节2
        metaData.push_back(static_cast<uint8_t>(DATA[i] & 0xFF));         // 字节3 (LSB)
    }
    return metaData;
}

}  // namespace [Dummy]

TEST_F(KernelContextTest, save_null_args_expect_return_and_got_empty_mem_info)
{
    string tempOutput = "temp_test_dir";
    auto &inst = KernelContext::Instance();
    inst.Reset();
    inst.SetArgsSize(nullptr);
    ASSERT_EQ(inst.GetOpMemInfo().inputParamsAddrInfos.size(), 0);
}

TEST_F(KernelContextTest, save_multi_args_expect_return_and_got_multi_mem_info)
{
    string tempOutput = "temp_test_dir";
    auto &inst = KernelContext::Instance();
    rtArgsSizeInfo sizeInfo;
    vector<uint64_t> sizeInfoData{1717, 11ULL<<32|2, 64, 128};
    sizeInfo.infoAddr = sizeInfoData.data();
    sizeInfo.atomicIndex = 0;
    inst.SetArgsSize(&sizeInfo);
    ASSERT_EQ(inst.GetOpMemInfo().inputParamsAddrInfos.size(), 2);
    ASSERT_EQ(inst.GetOpMemInfo().inputParamsAddrInfos[0].length, 64);
    ASSERT_EQ(inst.GetOpMemInfo().inputParamsAddrInfos[1].length, 128);
}

TEST_F(KernelContextTest, save_dump_null_args_expect_return_and_no_data_dump)
{
    string tempOutput = "temp_test_dir";
    auto &inst = KernelContext::Instance();
    inst.AddLaunchEvent(nullptr, 0, 1, nullptr, nullptr);
    MkdirRecusively(tempOutput);
    KernelContext::ContextConfig config;
    inst.GetDeviceContext().DumpKernelArgs(tempOutput, 0, config);
    ASSERT_FALSE(IsPathExists(tempOutput + "input_0.bin"));
    inst.Reset();
    RemoveAll(tempOutput);
}

TEST_F(KernelContextTest, fake_sink_kernel_args_then_dump_kernel_args_expect_return_true)
{
    string tempOutput = "temp_test_dir";
    MOCKER_CPP(&KernelContext::DeviceContext::GetPcStartAddr,
        bool(KernelContext::DeviceContext::*)(KernelContext::KernelHandleArgs const &, uint64_t&) const)
        .stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetNameByTilingKey).stubs().will(returnValue(string("kernelName")));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(invoke(MemcpyStub));
    MOCKER(&aclrtMallocHostImplOrigin).stubs().will(invoke(AclrtMallocHostStub));
    MOCKER(&aclrtFreeHostImplOrigin).stubs().will(invoke(AclrtFreeHostStub));

    auto &inst = KernelContext::Instance();
    // add sink stream
    uint32_t stm{};
    auto &streamInfo = LaunchManager::GetOrCreateStreamInfo(&stm);
    streamInfo.binded = true;

    // add launch
    rtArgsEx_t argsInfo{};

    vector<uint64_t> argsPtr(3);
    vector<vector<uint64_t>> argsData(argsPtr.size(), vector<uint64_t>(100, 1));
    for (int i = 0; i < argsPtr.size(); i++) {
        argsPtr[i] = reinterpret_cast<uint64_t>(&argsData[i]);
    }
    argsInfo.args = static_cast<void *>(argsPtr.data());
    inst.AddLaunchEvent(nullptr, 0, 1, &argsInfo, &stm);

    // add mem history
    auto &memInfo = inst.GetOpMemInfo();
    memInfo.inputParamsAddrInfos.push_back(KernelContext::AddrInfo{0, 1, MemInfoSrc::EXTRA});
    memInfo.inputParamsAddrInfos.push_back(KernelContext::AddrInfo{0, 2, MemInfoSrc::EXTRA});
    memInfo.inputParamsAddrInfos.push_back(KernelContext::AddrInfo{0, 3, MemInfoSrc::EXTRA});
    inst.GetDeviceContext().ArchiveMemInfo();

    MkdirRecusively(tempOutput);
    KernelContext::ContextConfig config;
    ASSERT_TRUE(inst.GetDeviceContext().DumpKernelArgs(tempOutput, 0, config));
    ASSERT_TRUE(IsPathExists(tempOutput + "/input_0.bin"));
    RemoveAll(tempOutput);
}

TEST_F(KernelContextTest, fake_zero_length_sink_kernel_args_then_dump_kernel_args_expect_return_true)
{
    string tempOutput = "temp_test_dir";
    MOCKER_CPP(&KernelContext::DeviceContext::GetPcStartAddr,
        bool(KernelContext::DeviceContext::*)(KernelContext::KernelHandleArgs const &, uint64_t&) const)
        .stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetNameByTilingKey).stubs().will(returnValue(string("kernelName")));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(invoke(MemcpyStub));
    MOCKER(&aclrtMallocHostImplOrigin).stubs().will(invoke(AclrtMallocHostStub));
    MOCKER(&aclrtFreeHostImplOrigin).stubs().will(invoke(AclrtFreeHostStub));

    auto &inst = KernelContext::Instance();
    // add sink stream
    uint32_t stm{};
    auto &streamInfo = LaunchManager::GetOrCreateStreamInfo(&stm);
    streamInfo.binded = true;

    // add launch
    rtArgsEx_t argsInfo{};

    vector<uint64_t> argsPtr(1);
    vector<vector<uint64_t>> argsData(argsPtr.size(), vector<uint64_t>(100, 0));
    for (int i = 0; i < argsPtr.size(); i++) {
        argsPtr[i] = reinterpret_cast<uint64_t>(&argsData[i]);
    }
    argsInfo.args = static_cast<void *>(argsPtr.data());
    inst.AddLaunchEvent(nullptr, 0, 1, &argsInfo, &stm);

    // add mem history
    auto &memInfo = inst.GetOpMemInfo();
    memInfo.inputParamsAddrInfos.push_back(KernelContext::AddrInfo{0, 0, MemInfoSrc::EXTRA});
    inst.GetDeviceContext().ArchiveMemInfo();

    MkdirRecusively(tempOutput);
    KernelContext::ContextConfig config;
    ASSERT_TRUE(inst.GetDeviceContext().DumpKernelArgs(tempOutput, 0, config));
    ASSERT_FALSE(IsPathExists(tempOutput + "/input_0.bin"));
    RemoveAll(tempOutput);
}

TEST_F(KernelContextTest, save_dump_valid_args_expect_return_and_normal_data_dump)
{
    string tempOutput = "temp_test_dir/";
    auto &inst = KernelContext::Instance();
    MkdirRecusively(tempOutput);
    MOCKER(&rtMemcpyOrigin).stubs().will(invoke(RtMemcpyStub));
    MOCKER(&rtMallocHostOrigin).stubs().will(invoke(RtMallocHostStub));
    MOCKER(&rtFreeHostOrigin).stubs().will(invoke(RtFreeHostStub));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(invoke(MemcpyStub));
    MOCKER(&aclrtMallocHostImplOrigin).stubs().will(invoke(AclrtMallocHostStub));
    MOCKER(&aclrtFreeHostImplOrigin).stubs().will(invoke(AclrtFreeHostStub));

    int argsNum = 2;
    uint64_t skipNum = 0;
    int argsSize0 = 64, argsSize1 = 128; // bytes
    int tilingSize = 256;
    rtArgsSizeInfo sizeInfo;
    vector<uint64_t> sizeInfoData{1717, skipNum<<32|argsNum, argsSize0, argsSize1};
    sizeInfo.infoAddr = sizeInfoData.data();
    sizeInfo.atomicIndex = 0;
    inst.SetArgsSize(&sizeInfo);

    vector<uint64_t> argsBuff(argsNum + tilingSize / sizeof(uint64_t), 12);
    vector<uint8_t> args0(argsSize0 / sizeof(uint8_t));
    vector<uint8_t> args1(argsSize1 / sizeof(uint8_t));
    argsBuff[0] = (uint64_t)args0.data();
    argsBuff[1] = (uint64_t)args1.data();
    rtArgsEx_t argsInfo;
    argsInfo.args = argsBuff.data();
    argsInfo.tilingDataOffset = argsNum * 8;
    argsInfo.tilingAddrOffset = 0;
    argsInfo.argsSize = argsBuff.size() * sizeof(uint64_t);

    rtDevBinary_t binary;
    // save memInfo_ to memInfoHistory
    inst.ParseMetaDataFromBinary(binary, &argsInfo);
    inst.ArchiveMemInfo();
    inst.AddLaunchEvent(nullptr, 0, 1, &argsInfo, nullptr);
    KernelContext::ContextConfig config;
    inst.GetDeviceContext().DumpKernelArgs(tempOutput, 0, config);

    ASSERT_TRUE(IsPathExists(tempOutput + "input_0.bin"));
    ASSERT_TRUE(IsPathExists(tempOutput + "input_1.bin"));
    ASSERT_TRUE(IsPathExists(tempOutput + "input_tiling.bin"));
    RemoveAll(tempOutput);
    inst.Reset();
}

TEST_F(KernelContextTest, build_names_with_valid_kernel_binary_expect_get_kernel_name_by_tiling_key_success)
{
    KernelContext::Instance().Reset();
    string mockContent = ("0000 g F .text 0000a8 Abs_d2db_high_performance_21474\n"
        "0010 g F .text 0000f8 Abs_cc2c_high_performance_3333\n"
        "0020 g F .text 000af8 Abs_dd3d_high_performance_4444\n");

    MOCKER(&PipeCall).stubs().with(any(), outBound(mockContent)).will(returnValue(true));
    char *ascendHomePath = "ascend_home_path";
    MOCKER(&secure_getenv).stubs().will(returnValue(ascendHomePath));
    string mocKObjdumpDir = JoinPath({ascendHomePath, "compiler", "ccec_compiler", "bin"});
    MkdirRecusively(mocKObjdumpDir);
    string mocKObjdumpPath = JoinPath({mocKObjdumpDir, "llvm-objdump"});
    // use any() may compile error
    /* const char *placeholder; */
    /* unsigned long x; */
    WriteBinary(mocKObjdumpPath, mockContent.data(), mockContent.length());
    chmod(mocKObjdumpPath.c_str(), EXEC_AUTHORITY);
    ASSERT_TRUE(IsPathExists(mocKObjdumpPath));
    MOCKER(&WriteBinary).stubs().will(returnValue(1UL));

    auto &inst = KernelContext::Instance();
    string tempOutput = "temp_test_dir/";
    vector<uint8_t> hdlData(tempOutput.size());
    void *handle = hdlData.data();
    rtDevBinary_t bin;
    bin.data = mockContent.data();
    bin.length = mockContent.length();
    inst.AddHdlRegisterEvent(handle, &bin);

    uint64_t tilignKey = 21474;
    string expect = "Abs_d2db_high_performance_21474";
    string got = inst.GetNameByTilingKey(handle, tilignKey);
    ASSERT_EQ(expect, got);

    tilignKey = 3333;
    expect = "Abs_cc2c_high_performance_3333";
    got = inst.GetNameByTilingKey(handle, tilignKey);
    ASSERT_EQ(expect, got);

    tilignKey = 4444;
    expect = "Abs_dd3d_high_performance_4444";
    got = inst.GetNameByTilingKey(handle, tilignKey);
    ASSERT_EQ(expect, got);

    RemoveAll(mocKObjdumpDir);
}

TEST_F(KernelContextTest, input_null_then_call_add_register_event_expect_return)
{
    uint64_t originSize = KernelContext::Instance().GetNextRegisterId();
    KernelContext::Instance().AddHdlRegisterEvent(nullptr, nullptr);
    ASSERT_EQ(KernelContext::Instance().GetNextRegisterId(), originSize);
}

TEST_F(KernelContextTest, setDeviceId_return_value_without_convert)
{
    DeviceContext::Local().SetDeviceId(10);
    MOCKER(&IsOpProf).stubs().will(returnValue(true));
    MessageOfProfConfig config;
    config.isSimulator = true;
    ProfConfig::Instance().Init(config);
    ASSERT_EQ(10, DeviceContext::Local().GetDeviceId());
}

TEST_F(KernelContextTest, input_no_register_event_then_save_expect_false)
{
    KernelContext::Instance().Reset();
    ASSERT_FALSE(KernelContext::Instance().Save(""));
}

TEST_F(KernelContextTest, input_no_launch_event_then_save_expect_false)
{
    KernelContext::Instance().Reset();
    vector<uint8_t> handleData{1, 2, 3};
    const KernelHandle *hdl = handleData.data();
    rtDevBinary_t bin{};
    KernelContext::Instance().AddHdlRegisterEvent(hdl, &bin);
    ASSERT_FALSE(KernelContext::Instance().Save(""));
}

TEST_F(KernelContextTest, mock_reg_launch_event_then_save_expect_true)
{
    MOCKER(&rtKernelGetAddrAndPrefCntOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));
    uint64_t originLaunchSize = KernelContext::Instance().GetDeviceContext().launchEvents_.size();
    uint64_t originRegisterSize = KernelContext::Instance().registerEvents_.size();

    bool(KernelContext::*funcPtr)(uint64_t, const string&) = &KernelContext::DumpKernelObject;
    MOCKER(funcPtr).stubs().will(returnValue(true));
    MOCKER(&KernelContext::DeviceContext::DumpKernelArgs).stubs().will(returnValue(true));
    MOCKER(&GetSymInfoFromBinary).stubs().will(returnValue(true));
    nlohmann::json jsonData;
    jsonData["bin_path"] = "abc";
    MOCKER(&KernelContext::ContextConfig::ToJson).stubs().with(outBound(jsonData)).will(returnValue(true));
    MOCKER(&WriteBinary).stubs().will(returnValue(jsonData.dump().length()));
    KernelContext::Instance().ArchiveMemInfo();

    vector<uint8_t> handleData{1, 2, 3};
    const KernelHandle *hdl = handleData.data();
    rtDevBinary_t bin;
    vector<char> data(8, 1);
    bin.data = data.data();
    bin.length = data.size();
    bin.magic = 15;
    KernelContext::Instance().AddHdlRegisterEvent(hdl, &bin);
    KernelContext::Instance().AddLaunchEvent(hdl, 111, 8, nullptr, nullptr);
    auto launchId = KernelContext::Instance().GetLaunchId();
    EXPECT_EQ(KernelContext::Instance().GetDeviceContext().launchEvents_.size(), originLaunchSize + 1);
    EXPECT_EQ(KernelContext::Instance().registerEvents_.size(), originRegisterSize + 1);
    EXPECT_TRUE(KernelContext::Instance().Save("./", launchId));
    EXPECT_TRUE(KernelContext::Instance().SaveAicore("./", launchId));
}

TEST_F(KernelContextTest, set_null_hdl_then_test_dump_kernel_object_expect_fail)
{
    EXPECT_FALSE(KernelContext::Instance().DumpKernelObject(nullptr, "", ""));
}

size_t CheckResult(const string &filename, const char *data, uint64_t length)
{
    Elf64_Ehdr header{};
    vector<char> buffer(data, data + length);
    EXPECT_TRUE(ElfLoader::LoadHeader(buffer, header));
    EXPECT_EQ(header.e_flags, FLAG_A2_AIV);
    return length;
}

TEST_F(KernelContextTest, runtime_get_pcstart_failed_expect_get_pcstart_by_stub_func_fail)
{
    MOCKER(&rtKernelGetAddrAndPrefCntOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));

    uint64_t pcStartAddr;
    ASSERT_FALSE(KernelContext::Instance().GetPcStartAddr(KernelContext::StubFuncPtr{nullptr}, pcStartAddr));
}

TEST_F(KernelContextTest, call_rtKernelGetAddrAndPrefCntOrigin_failed_expect_get_pcstart_by_stub_func_fail)
{
    MOCKER(&rtKernelGetAddrAndPrefCntOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));

    auto &context = KernelContext::Instance();
    uint64_t pcStartAddr;
    ASSERT_FALSE(context.GetPcStartAddr(KernelContext::StubFuncPtr{nullptr}, pcStartAddr));
}

TEST_F(KernelContextTest, get_stub_func_info_failed_expect_get_pcstart_by_stub_func_fail)
{
    MOCKER(&rtKernelGetAddrAndPrefCntOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    MOCKER(&KernelContext::GetStubFuncInfo).stubs().will(returnValue(false));

    auto &context = KernelContext::Instance();
    uint64_t pcStartAddr;
    ASSERT_FALSE(context.GetPcStartAddr(KernelContext::StubFuncPtr{nullptr}, pcStartAddr));
}

TEST_F(KernelContextTest, get_register_event_failed_expect_get_pcstart_by_stub_func_fail)
{
    MOCKER(&rtKernelGetAddrAndPrefCntOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    MOCKER(&KernelContext::GetStubFuncInfo).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetRegisterEvent).stubs().will(returnValue(false));

    auto &context = KernelContext::Instance();
    uint64_t pcStartAddr;
    ASSERT_FALSE(context.GetPcStartAddr(KernelContext::StubFuncPtr{nullptr}, pcStartAddr));
}

TEST_F(KernelContextTest, register_binary_with_empty_kernel_names_expect_get_pcstart_by_stub_func_fail)
{
    MOCKER(&rtKernelGetAddrAndPrefCntOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    MOCKER(&KernelContext::GetStubFuncInfo).stubs().will(returnValue(true));
    MOCKER(&KernelContext::GetRegisterEvent).stubs().will(returnValue(true));
    MOCKER(&GetSymInfoFromBinary).stubs();

    auto &context = KernelContext::Instance();
    uint64_t pcStartAddr;
    ASSERT_FALSE(context.GetPcStartAddr(KernelContext::StubFuncPtr{nullptr}, pcStartAddr));
}

bool GetValidNameFromBinary(const char *data,
                            uint64_t length,
                            std::vector<std::string> &kernelNames,
                            std::vector<uint64_t> &kernelOffsets)
{
    kernelNames.emplace_back("valid_kernel_1234_mix_aic");
    kernelOffsets.emplace_back(0);
    return true;
}

TEST_F(KernelContextTest, register_binary_with_valid_kernel_names_expect_get_pcstart_by_stub_func_success)
{
    MOCKER(&rtKernelGetAddrAndPrefCntOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    MOCKER(&GetSymInfoFromBinary).stubs().will(invoke(GetValidNameFromBinary));

    auto &context = KernelContext::Instance();
    auto hdl = reinterpret_cast<KernelHandle const *>(0x1234);
    rtDevBinary_t bin {};
    context.AddHdlRegisterEvent(hdl, &bin);
    context.AddFuncRegisterEvent(hdl, nullptr, "valid_kernel_1234_mix_aic", "valid_kernel_1234_mix_aic", 0);

    uint64_t pcStartAddr;
    ASSERT_TRUE(context.GetPcStartAddr(KernelContext::StubFuncPtr{nullptr}, pcStartAddr));

    context.Reset();
}

TEST_F(KernelContextTest, hdl_to_regid_empty_expect_get_pcstart_by_hdl_fail)
{
    uint64_t pcStartAddr;
    ASSERT_FALSE(KernelContext::Instance().GetPcStartAddr(KernelContext::KernelHandlePtr{nullptr}, pcStartAddr));
}

TEST_F(KernelContextTest, register_binary_with_empty_kernel_names_expect_get_pcstart_by_hdl_fail)
{
    std::vector<std::string> kernelNames;
    MOCKER(&GetSymInfoFromBinary).stubs().will(returnValue(true));

    auto &context = KernelContext::Instance();
    auto hdl = reinterpret_cast<KernelHandle const *>(0x1234);
    rtDevBinary_t bin {};
    context.AddHdlRegisterEvent(hdl, &bin);
    uint64_t pcStartAddr;
    ASSERT_FALSE(context.GetPcStartAddr(KernelContext::KernelHandlePtr{hdl}, pcStartAddr));
}

bool GetInvalidNameFromBinary(const char *data,
                              uint64_t length,
                              std::vector<std::string> &kernelNames,
                              std::vector<uint64_t> &kernelOffsets)
{
    kernelNames.emplace_back("invalidkernel_mix_aic");
    kernelOffsets.emplace_back(0);
    return true;
}

TEST_F(KernelContextTest, register_binary_with_invalid_kernel_names_expect_get_pcstart_by_hdl_fail)
{
    MOCKER(&GetSymInfoFromBinary).stubs().will(invoke(GetInvalidNameFromBinary));
    auto &context = KernelContext::Instance();
    auto hdl = reinterpret_cast<KernelHandle const *>(0x1234);
    rtDevBinary_t bin {};
    context.AddHdlRegisterEvent(hdl, &bin);
    uint64_t pcStartAddr;
    ASSERT_FALSE(context.GetPcStartAddr(KernelContext::KernelHandlePtr{hdl}, pcStartAddr));
}

TEST_F(KernelContextTest, register_binary_with_valid_kernel_names_expect_get_pcstart_by_hdl_success)
{
    MOCKER(&GetSymInfoFromBinary).stubs().will(invoke(GetValidNameFromBinary));
    MOCKER(&rtKernelGetAddrAndPrefCntOrigin).stubs().will(returnValue(RT_ERROR_NONE));

    auto &context = KernelContext::Instance();
    auto hdl = reinterpret_cast<KernelHandle const *>(0x1234);
    rtDevBinary_t bin {};
    context.AddHdlRegisterEvent(hdl, &bin);
    uint64_t pcStartAddr;
    ASSERT_TRUE(context.GetPcStartAddr(KernelContext::KernelHandlePtr{hdl}, pcStartAddr));
}

TEST_F(KernelContextTest, runtime_get_pcstart_failed_expect_get_pcstart_by_hdl_fail)
{
    MOCKER(&GetSymInfoFromBinary).stubs().will(invoke(GetValidNameFromBinary));
    MOCKER(&rtKernelGetAddrAndPrefCntOrigin).stubs().will(returnValue(RT_ERROR_RESERVED));

    auto &context = KernelContext::Instance();
    auto hdl = reinterpret_cast<KernelHandle const *>(0x1234);
    rtDevBinary_t bin {};
    context.AddHdlRegisterEvent(hdl, &bin);
    uint64_t pcStartAddr;
    ASSERT_FALSE(context.GetPcStartAddr(KernelContext::KernelHandlePtr{hdl}, pcStartAddr));
}

TEST_F(KernelContextTest, add_launch_event_then_test_get_dev_binary_expect_success)
{
    vector<uint8_t> handleData{1, 2, 3};
    const KernelHandle *hdl = handleData.data();
    rtDevBinary_t bin;
    vector<char> data(8, 1);
    bin.data = data.data();
    bin.length = data.size();
    bin.magic = 15;
    KernelContext::Instance().AddHdlRegisterEvent(hdl, &bin);
    KernelContext::Instance().AddLaunchEvent(hdl, 111, 8, nullptr, nullptr);
    EXPECT_TRUE(KernelContext::Instance().GetDevBinary(KernelContext::KernelHandlePtr{hdl}, bin));
}

TEST_F(KernelContextTest, add_stub_launch_event_then_test_get_dev_binary_expect_success)
{
    vector<uint8_t> handleData{1, 2, 3};
    const StubFunc *hdl = handleData.data();
    rtDevBinary_t bin;
    vector<char> data(8, 1);
    bin.data = data.data();
    bin.length = data.size();
    bin.magic = 15;
    KernelContext::Instance().AddHdlRegisterEvent(hdl, &bin);
    KernelContext::Instance().AddFuncRegisterEvent(hdl, hdl, nullptr, nullptr, 0);
    KernelContext::Instance().AddLaunchEvent(hdl, 8, nullptr, nullptr);

    EXPECT_TRUE(KernelContext::Instance().GetDevBinary(KernelContext::StubFuncPtr{hdl}, bin));
}

void GetContextConfig(KernelContext::ContextConfig &config, int magic)
{
    config.binPath = "aicore.0.bin";
    config.blockDim = 8;
    config.visibleDevId = 1;
    config.devId = 0;
    config.isFFTS = true;
    config.inputPathList = {"input1", "input2", "input3"};
    config.inputSizeList = {8, 16, 0};
    config.kernelName = "mix_abc";
    config.magic = magic;
    config.tilingDataPath = "tiling_data.path";
    config.tilingDataSize = 200;
    config.hasTilingKey = true;
    config.tilingKey = 111;
}

TEST_F(KernelContextTest, input_valid_config_then_call_to_json_expect_correct_result)
{
    KernelContext::ContextConfig config;
    GetContextConfig(config, rtDevBinaryMagicElfAivec);
    nlohmann::json jsonData;
    ASSERT_TRUE(config.ToJson(jsonData));
    string expect = R"+*({"bin_path":"aicore.0.bin","block_dim":"8","device_id":"0",)+*";
    expect += R"+*("ffts":"Y","input_path":"input1;input2;input3","input_size":"8;16;0",)+*";
    expect += R"+*("kernel_name":"mix_abc","magic":"RT_DEV_BINARY_MAGIC_ELF_AIVEC",)+*";
    expect += R"+*("tiling_data_path":"tiling_data.path;200","tiling_key":"111"})+*";
    string result = jsonData.dump();
    EXPECT_EQ(result, expect);
}


TEST_F(KernelContextTest, input_valid_aicube_config_then_call_to_json_expect_correct_result)
{
    KernelContext::ContextConfig config;
    GetContextConfig(config, rtDevBinaryMagicElfAicube);
    nlohmann::json jsonData;
    ASSERT_TRUE(config.ToJson(jsonData));
    string expect = R"+*({"bin_path":"aicore.0.bin","block_dim":"8","device_id":"0",)+*";
    expect += R"+*("ffts":"Y","input_path":"input1;input2;input3","input_size":"8;16;0",)+*";
    expect += R"+*("kernel_name":"mix_abc","magic":"RT_DEV_BINARY_MAGIC_ELF_AICUBE",)+*";
    expect += R"+*("tiling_data_path":"tiling_data.path;200","tiling_key":"111"})+*";
    string result = jsonData.dump();
    EXPECT_EQ(result, expect);
}


TEST_F(KernelContextTest, input_valid_mix_config_then_call_to_json_expect_correct_result)
{
    KernelContext::ContextConfig config;
    GetContextConfig(config, rtDevBinaryMagicElf);
    nlohmann::json jsonData;
    ASSERT_TRUE(config.ToJson(jsonData));
    string expect = R"+*({"bin_path":"aicore.0.bin","block_dim":"8","device_id":"0",)+*";
    expect += R"+*("ffts":"Y","input_path":"input1;input2;input3","input_size":"8;16;0",)+*";
    expect += R"+*("kernel_name":"mix_abc","magic":"RT_DEV_BINARY_MAGIC_ELF",)+*";
    expect += R"+*("tiling_data_path":"tiling_data.path;200","tiling_key":"111"})+*";
    string result = jsonData.dump();
    EXPECT_EQ(result, expect);
}

TEST_F(KernelContextTest, input_empty_tiling_config_then_call_to_json_expect_correct_result)
{
    KernelContext::ContextConfig config;
    GetContextConfig(config, rtDevBinaryMagicElf);
    config.tilingDataPath.clear();
    nlohmann::json jsonData;
    ASSERT_TRUE(config.ToJson(jsonData));
    string expect = R"+*({"bin_path":"aicore.0.bin","block_dim":"8","device_id":"0",)+*";
    expect += R"+*("ffts":"Y","input_path":"input1;input2;input3","input_size":"8;16;0",)+*";
    expect += R"+*("kernel_name":"mix_abc","magic":"RT_DEV_BINARY_MAGIC_ELF",)+*";
    expect += R"+*("tiling_key":"111"})+*";
    string result = jsonData.dump();
    EXPECT_EQ(result, expect);
}

TEST_F(KernelContextTest, GetStubFuncInfo_return_false)
{
    KernelContext::StubFuncPtr stubFunc;
    KernelContext::StubFuncInfo stubFuncInfo;
    KernelContext::Instance().stubInfo_;
    KernelContext::Instance().GetStubFuncInfo(stubFunc, stubFuncInfo);
}

TEST_F(KernelContextTest, input_no_launch_event_then_save_aicore_expect_false)
{
    KernelContext::Instance().Reset();
    vector<uint8_t> handleData{1, 2, 3};
    const KernelHandle *hdl = handleData.data();
    rtDevBinary_t bin{};
    KernelContext::Instance().AddHdlRegisterEvent(hdl, &bin);
    ASSERT_FALSE(KernelContext::Instance().SaveAicore(""));
}

TEST_F(KernelContextTest, get_dump_context_bin_is_nullptr)
{
    KernelContext::Instance().Reset();
    rtDevBinary_t binary;
    binary.data = nullptr;
    binary.length = 0;
    KernelContext::Instance().ParseMetaDataFromBinary(binary);
    ASSERT_TRUE(KernelContext::Instance().GetOpMemInfo().inputParamsAddrInfos.empty());
}

TEST_F(KernelContextTest, get_dump_context_dynamic_kernel)
{
    KernelContext::Instance().Reset();
    rtDevBinary_t binary;
    std::vector<uint8_t> metaData = {0x01, 0x00, 0x04, 0x00, 0x03, 0x00, 0x00, 0x00, 0x06, 0x00, 0x04, 0x00, 0x00,
                                     0x00, 00, 00, 04, 00, 0x50, 00, 00, 0x05, 00, 02, 00, 00, 00, 00, 00, 01, 00, 01,
                                     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 00, 05, 00, 02, 00, 00, 00, 00,
                                     00, 01, 00, 01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 00, 05, 00, 02, 00,
                                     00, 00, 00, 00, 01, 00, 01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 00, 05,
                                     00, 02, 00, 00, 00, 00, 00, 01, 00, 07, 00, 00, 00, 00, 00, 00, 00, 0x20};
    MOCKER(&GetMetaSection).stubs().with(any(), any(), outBound(metaData))
    .will(returnValue(metaData.size()));
    KernelContext::Instance().ParseMetaDataFromBinary(binary);
    ASSERT_EQ(KernelContext::Instance().GetOpMemInfo().inputParamsAddrInfos.size(), 3);
}

TEST_F(KernelContextTest, get_dump_context_static_kernel)
{
    KernelContext::Instance().Reset();
    rtDevBinary_t binary;
    std::vector<uint8_t> metaData = {0x01, 0x00, 0x04, 0x00, 0x03, 0x00, 0x00, 0x00, 0x06, 0x00, 0x04, 0x00,
                                     0x00, 0x00, 00, 00, 04, 00, 0x50, 00, 00, 0x05, 00, 02, 00, 00, 00, 00, 00, 01,
                                     00, 01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 00, 05, 00, 02, 00, 00,
                                     00, 00, 00, 01, 00, 01, 00, 00, 00, 00, 00, 0x00, 0x00, 0x20, 00, 05, 00, 02,
                                     00, 00, 00, 00, 00, 01, 00, 01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
                                     00, 05, 00, 02, 00, 00, 00, 00, 00, 01, 00, 07, 00, 00, 00, 00, 00, 00, 00, 0x20};
    MOCKER(&GetMetaSection).stubs().with(any(), any(), outBound(metaData))
    .will(returnValue(metaData.size()));
    KernelContext::Instance().ParseMetaDataFromBinary(binary);
    ASSERT_EQ(KernelContext::Instance().GetOpMemInfo().inputParamsAddrInfos.size(), 3);
    ASSERT_EQ(KernelContext::Instance().GetOpMemInfo().inputParamsAddrInfos[0].length, 32);
}

TEST_F(KernelContextTest, mock_dynamic_ascendc_kernel_valid_meta_section_then_parse_it_expect_success)
{
    rtDevBinary_t binary;
    auto metaData = MockDyanmicAscendcKernelMetaSection();
    MOCKER(&GetMetaSection).stubs().with(any(), any(), outBound(metaData))
    .will(returnValue(metaData.size()));
    // fake args
    rtArgsEx_t argsInfo{};
    const size_t tilingSize = 32;
    // i,i,o,tiling_addr,tiling_data(atomicIndex)
    vector<uint64_t> argsPtr(4 + tilingSize);
    argsPtr.back() = 0xA5A5A5A50000;
    const size_t addInputSize = 32768;
    vector<vector<uint64_t>> argsData(4, vector<uint64_t>(addInputSize, 1));
    for (int i = 0; i < argsData.size(); i++) {
        argsPtr[i] = reinterpret_cast<uint64_t>(&argsData[i]);
    }
    argsInfo.args = static_cast<void *>(argsPtr.data());
    argsInfo.args = static_cast<void *>(argsPtr.data());
    argsInfo.argsSize = argsPtr.size() * sizeof(uint64_t);
    argsInfo.tilingDataOffset = sizeof(uint64_t) * 4;
    auto &inst = KernelContext::Instance();
    // fake adump autoIdex
    size_t argsSpace = 96;
    vector<uint64_t> adumpData(argsSpace, 1);
    AdumpInfo context{adumpData.data(), argsSpace};
    ArgsManager::Instance().AddAdumpInfo(0, context);

    inst.ParseMetaDataFromBinary(binary, &argsInfo);
    ASSERT_EQ(inst.GetOpMemInfo().inputParamsAddrInfos.size(), 4);
    ASSERT_EQ(inst.GetOpMemInfo().inputParamsAddrInfos[0].length, 1);
    ASSERT_EQ(inst.GetOpMemInfo().inputParamsAddrInfos[1].length, 1);
    ASSERT_EQ(inst.GetOpMemInfo().inputParamsAddrInfos[2].length, 1);
    ASSERT_EQ(inst.GetOpMemInfo().inputParamsAddrInfos[3].length, 1);
}

TEST_F(KernelContextTest, mock_no_adump_addr_then_parse_it_expect_fail)
{
    rtDevBinary_t binary;
    auto metaData = MockDyanmicAscendcKernelMetaSection();
    MOCKER(&GetMetaSection).stubs().with(any(), any(), outBound(metaData))
    .will(returnValue(metaData.size()));
    // fake args
    rtArgsEx_t argsInfo{};
    const size_t tilingSize = 32;
    // i,i,o,tiling_addr,tiling_data(atomicIndex)
    vector<uint64_t> argsPtr(4 + tilingSize);
    argsPtr.back() = 0xA5A5A5A50000;
    const size_t addInputSize = 32768;
    vector<vector<uint64_t>> argsData(4, vector<uint64_t>(addInputSize, 1));
    for (int i = 0; i < argsPtr.size(); i++) {
        argsPtr[i] = reinterpret_cast<uint64_t>(&argsData[i]);
    }
    argsInfo.args = static_cast<void *>(argsPtr.data());
    argsInfo.argsSize = argsPtr.size() * sizeof(uint64_t);
    argsInfo.tilingDataOffset = sizeof(uint64_t) * 4;

    auto &inst = KernelContext::Instance();
    inst.ParseMetaDataFromBinary(binary, &argsInfo);
    ASSERT_TRUE(inst.GetOpMemInfo().inputParamsAddrInfos.empty());
}

TEST_F(KernelContextTest, get_tik_atomic_index)
{
    KernelContext::Instance().Reset();

    rtArgsEx_t argsInfo;
    argsInfo.tilingDataOffset = 4;
    vector<uint8_t> v = {0x01, 0x01, 0x01, 0x02, 0xA5, 0xA5, 0xA5, 0xA5, 0x01, 0x01, 0x01, 0x01};
    argsInfo.args = v.data();
    rtDevBinary_t binary;
    std::vector<uint8_t> metaData = {0x01, 0x00, 0x04, 0x00, 0x03, 0x00, 0x00, 0x00, 0x06, 0x00, 0x04, 0x00,
                                     0x00, 0x00, 00, 00, 04, 00, 0x50, 00, 00, 0x05, 00, 02, 00, 00, 00, 00, 00, 01,
                                     00, 01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 00, 05, 00, 02, 00, 00,
                                     00, 00, 00, 01, 00, 01, 00, 00, 00, 00, 0x00, 0x00, 0x00, 0x20, 00, 05, 00, 02,
                                     00, 00, 00, 00, 00, 01, 00, 01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
                                     00, 05, 00, 02, 00, 00, 00, 00, 00, 01, 00, 07, 00, 00, 00, 00, 00, 00, 00, 0x20};
    MOCKER(&GetMetaSection).stubs().with(any(), any(), outBound(metaData))
            .will(returnValue(metaData.size()));
    KernelContext::Instance().GetOpMemInfo().tilingDataSize = 8;
    KernelContext::Instance().GetOpMemInfo().isTik = true;
    ArgsManager::Instance().AddAdumpInfo(0xA5A5A5A501010101, {nullptr, 0});
    KernelContext::Instance().ParseMetaDataFromBinary(binary, &argsInfo);

    ASSERT_EQ(KernelContext::Instance().GetOpMemInfo().inputParamsAddrInfos.size(), 0);
}

TEST_F(KernelContextTest, get_meta_section) {
    std::vector<uint8_t> metaData;
    rtDevBinary_t binary{};
    ASSERT_EQ(GetMetaSection(binary, "abc", metaData), 0);
}

// 构造包含 .ascend.meta.<kernelName> section 的最小合法 ELF
static std::vector<uint8_t> CreateElfWithMetaSection(const std::string &kernelName, Elf64_Xword dataSize) {
    std::string nameTable;
    nameTable.push_back('\0');
    nameTable += ".shstrtab";
    nameTable.push_back('\0');
    nameTable += ".ascend.meta.";
    nameTable += kernelName;
    nameTable.push_back('\0');
    // name offsets: null=0, .shstrtab=1, .ascend.meta.xxx=11
    Elf64_Word metaNameOff = static_cast<Elf64_Word>(1 + strlen(".shstrtab") + 1);

    Elf64_Ehdr header{};
    header.e_ident[EI_MAG0] = ELFMAG0;
    header.e_ident[EI_MAG1] = ELFMAG1;
    header.e_ident[EI_MAG2] = ELFMAG2;
    header.e_ident[EI_MAG3] = ELFMAG3;
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_shnum = 3; // null + .shstrtab + meta
    header.e_shstrndx = 1;
    header.e_shentsize = sizeof(Elf64_Shdr);
    header.e_shoff = sizeof(Elf64_Ehdr);

    Elf64_Shdr shstrtabHdr{};
    shstrtabHdr.sh_name = 1;
    shstrtabHdr.sh_type = SHT_STRTAB;
    shstrtabHdr.sh_offset = sizeof(Elf64_Ehdr) + 3 * sizeof(Elf64_Shdr);
    shstrtabHdr.sh_size = nameTable.size();

    Elf64_Shdr metaHdr{};
    metaHdr.sh_name = metaNameOff;
    metaHdr.sh_offset = shstrtabHdr.sh_offset + shstrtabHdr.sh_size;
    metaHdr.sh_size = dataSize;

    size_t totalSize = metaHdr.sh_offset + metaHdr.sh_size;
    std::vector<uint8_t> elf(totalSize);
    auto *p = elf.data();

    memcpy(p, &header, sizeof(header));
    p += sizeof(header);
    Elf64_Shdr nullHdr{};
    memcpy(p, &nullHdr, sizeof(nullHdr));
    p += sizeof(nullHdr);
    memcpy(p, &shstrtabHdr, sizeof(shstrtabHdr));
    p += sizeof(shstrtabHdr);
    memcpy(p, &metaHdr, sizeof(metaHdr));
    p += sizeof(metaHdr);
    memcpy(p, nameTable.data(), nameTable.size());
    // meta section data is already zeroed

    return elf;
}

TEST_F(KernelContextTest, mock_valid_section_head_then_test_get_meta_section_expect_success)
{
    std::vector<uint8_t> elf = CreateElfWithMetaSection("kernel_name", 5);
    rtDevBinary_t binary{};
    binary.data = elf.data();
    binary.length = elf.size();
    std::vector<uint8_t> metaData;
    ASSERT_EQ(GetMetaSection(binary, "kernel_name", metaData), 5u);
}

TEST_F(KernelContextTest, mock_ffts_size_info_then_set_args_size_expect_success)
{
    rtArgsSizeInfo_t sizeInfo;
    vector<uint64_t> argsData = {
        0, 1ULL << 32 | 2,
        1ULL << 56 | 1,
        2ULL << 56 | 1,
    };
    sizeInfo.infoAddr = static_cast<void *>(argsData.data());
    KernelContext::Instance().GetDeviceContext().SetArgsSize(&sizeInfo);
    ASSERT_TRUE(KernelContext::Instance().GetDeviceContext().GetOpMemInfo().isFFTS);
}

TEST_F(KernelContextTest, mock_rt_report_then_repeat_subscribe_expect_ok)
{
    MOCKER(&rtSetDeviceOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    MOCKER(&aclrtProcessReportImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    MOCKER(&aclrtSubscribeReportImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    uint64_t stm{};
    auto &inst = KernelContext::Instance();
    ASSERT_TRUE(inst.GetDeviceContext().SubscribeReport(&stm));
    auto &streamInfo = LaunchManager::GetOrCreateStreamInfo(&stm);
    ASSERT_TRUE(streamInfo.subscribed);
    ASSERT_TRUE(inst.GetDeviceContext().SubscribeReport(&stm));
}

TEST_F(KernelContextTest, mock_rt_report_fail_then_subscribe_expect_ok)
{
    MOCKER(&rtSetDeviceOrigin).stubs().will(returnValue(RT_ERROR_NONE));
    MOCKER(&aclrtProcessReportImplOrigin).stubs().will(returnValue(ACL_ERROR_INTERNAL_ERROR));
    MOCKER(&aclrtSubscribeReportImplOrigin).stubs().will(returnValue(ACL_ERROR_NONE));
    uint64_t stm{};
    auto &inst = KernelContext::Instance();
    ASSERT_TRUE(inst.GetDeviceContext().SubscribeReport(&stm));
}

TEST_F(KernelContextTest, mock_sym_info_then_set_dbi_binary_expect_true)
{
    MOCKER(&GetSymInfoFromBinary).stubs().will(returnValue(true));
    vector<uint8_t> handleData{1, 2, 3};
    const KernelHandle *hdl = handleData.data();
    rtDevBinary_t bin{};
    KernelContext::Instance().AddHdlRegisterEvent(hdl, &bin);
    ASSERT_TRUE(KernelContext::Instance().SetDbiBinary(0, bin));
}

TEST_F(KernelContextTest, mock_get_pc_start_addr_then_update_pc_start_expect_success)
{
    MOCKER_CPP(&KernelContext::DeviceContext::GetPcStartAddr,
        bool(KernelContext::DeviceContext::*)(KernelContext::KernelHandleArgs const &, uint64_t&) const)
        .stubs().will(returnValue(true));
    KernelContext::Instance().AddLaunchEvent(nullptr, 0, 1, nullptr, nullptr);
    KernelContext::KernelHandleArgs args{};
    ASSERT_TRUE(KernelContext::Instance().GetDeviceContext().UpdatePcStartAddrByDbi(0, args));
}
