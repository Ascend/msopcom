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

#include <cstring>
#include "runtime/inject_helpers/LaunchArgs.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <vector>

#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/ArgsManager.h"

using namespace std;

class ArgsManagerTest : public testing::Test {
public:
    void SetUp() override { }

    void TearDown() override
    {
        GlobalMockObject::verify();
        ArgsManager::Instance().Clear();
    }
};

TEST_F(ArgsManagerTest, input_valid_then_create_args_handle_context_expect_success)
{
    uint64_t placeholder;
    aclrtFuncHandle funcHandle = &placeholder;
    aclrtArgsHandle argsHandle = &placeholder;
    auto expect = ArgsManager::Instance().CreateContext(funcHandle, argsHandle);
    ASSERT_NE(expect, nullptr);
    auto ctx = ArgsManager::Instance().GetContext(argsHandle);
    ASSERT_EQ(expect, ctx);
}
TEST_F(ArgsManagerTest, second_pointer_host_shape_produces_input_address_and_byte_length) {
    // hostInputs: offset to the first address, then one 2D tensor shape and its device address.
    uint64_t hostArgs[] = {0, 4 * sizeof(uint64_t), (uint64_t{1} << 32) | 2, 3, 4, 0x12340000};
    aclrtPlaceHolderInfo placeholder{0, sizeof(uint64_t)};
    AclrtLaunchArgsInfo launchArgs{};
    launchArgs.hostArgs = hostArgs;
    launchArgs.argsSize = sizeof(hostArgs);
    launchArgs.placeHolderArray = &placeholder;
    launchArgs.placeHolderNum = 1;
    OpMemInfo opMemInfo{};
    opMemInfo.secondPtrAddrInfos[0].dtypeBytes = 4;
    ArgsManager::Instance().ParseSecondPtrAddrs(launchArgs, opMemInfo, 0);
    const auto &addresses = opMemInfo.secondPtrAddrInfos.at(0).addrInfoVec;
    ASSERT_EQ(addresses.size(), 1U);
    EXPECT_EQ(addresses[0].addr, 0x12340000U);
    EXPECT_EQ(addresses[0].length, 3U * 4U * 4U);
    EXPECT_EQ(addresses[0].memInfoSrc, MemInfoSrc::EXTRA);
}
TEST(LaunchArgsTest, appending_meminfo_aligns_plain_args_without_changing_input_bytes) {
    std::vector<uint8_t> original{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    rtArgsEx_t args{};
    args.args = original.data();
    args.argsSize = original.size();
    EXPECT_EQ(GetKernelParamNum(&args), 2U);
    EXPECT_EQ(GetArgsSize(&args), 16U);
    uint8_t memInfo = 42;
    std::vector<uint8_t> expanded;
    std::vector<rtHostInputInfo_t> hostInputs;
    uint32_t offset = 0;
    ASSERT_TRUE(ExpandArgs(&args, expanded, &memInfo, hostInputs, offset));
    EXPECT_EQ(offset, 16U);
    EXPECT_EQ(args.args, expanded.data());
    EXPECT_EQ(args.argsSize, 24U);
    EXPECT_EQ(std::memcmp(expanded.data(), original.data(), original.size()), 0);
    uint8_t *storedMemInfo = nullptr;
    std::memcpy(&storedMemInfo, expanded.data() + offset, sizeof(storedMemInfo));
    EXPECT_EQ(storedMemInfo, &memInfo);
    EXPECT_TRUE(hostInputs.empty());
    std::vector<uint8_t> raw{3, 4, 5};
    void *rawArgs = raw.data();
    uint32_t rawSize = raw.size();
    ASSERT_TRUE(ExpandArgs(rawArgs, rawSize, expanded, &memInfo));
    EXPECT_EQ(rawArgs, expanded.data());
    EXPECT_EQ(rawSize, 16U);
    EXPECT_EQ(std::memcmp(expanded.data(), raw.data(), raw.size()), 0);
    std::memcpy(&storedMemInfo, expanded.data() + 8, sizeof(storedMemInfo));
    EXPECT_EQ(storedMemInfo, &memInfo);
}
TEST(LaunchArgsTest, inserting_meminfo_before_tiling_preserves_host_input_offsets) {
    std::vector<uint8_t> original(32);
    for (size_t i = 0; i < original.size(); ++i) {
        original[i] = static_cast<uint8_t>(i);
    }
    rtHostInputInfo_t input{0, 16};
    rtArgsEx_t args{};
    args.args = original.data();
    args.argsSize = original.size();
    args.hasTiling = 1;
    args.tilingAddrOffset = 8;
    args.tilingDataOffset = 16;
    args.hostInputInfoPtr = &input;
    args.hostInputInfoNum = 1;
    EXPECT_EQ(GetKernelParamNum(&args), 2U);
    EXPECT_EQ(GetArgsSize(&args), 16U);
    uint8_t memInfo = 42;
    std::vector<uint8_t> expanded;
    std::vector<rtHostInputInfo_t> hostInputs;
    uint32_t offset = 0;
    ASSERT_TRUE(ExpandArgs(&args, expanded, &memInfo, hostInputs, offset));
    EXPECT_EQ(offset, 16U);
    EXPECT_EQ(args.argsSize, 40U);
    EXPECT_EQ(args.tilingDataOffset, 24U);
    ASSERT_EQ(hostInputs.size(), 1U);
    EXPECT_EQ(hostInputs[0].addrOffset, 0U);
    EXPECT_EQ(hostInputs[0].dataOffset, 24U);
    EXPECT_EQ(args.hostInputInfoPtr, hostInputs.data());
    EXPECT_EQ(std::memcmp(expanded.data(), original.data(), 16), 0);
    EXPECT_EQ(std::memcmp(expanded.data() + 24, original.data() + 16, 16), 0);
    uint8_t *storedMemInfo = nullptr;
    std::memcpy(&storedMemInfo, expanded.data() + offset, sizeof(storedMemInfo));
    EXPECT_EQ(storedMemInfo, &memInfo);
    original.assign(16, 0x5A);
    args = {};
    args.args = original.data();
    args.argsSize = original.size();
    args.hasTiling = 1;
    args.tilingAddrOffset = 16;
    EXPECT_FALSE(ExpandArgs(&args, expanded, &memInfo, hostInputs, offset));
    EXPECT_EQ(args.args, original.data());
    EXPECT_EQ(args.argsSize, original.size());
    EXPECT_EQ(args.tilingDataOffset, 0U);
}
