/* -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
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

#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "acl_rt_impl/AscendclImplOrigin.h"
#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/ArgsContext.h"
#include "runtime/inject_helpers/KernelMetaDataParser.h"
#include "utils/FileSystem.h"
#include "utils/Protocol.h"

using namespace std;

namespace {

// 没有运行期 tiling 时（动态输入 / tensor list 在 graph 特化编译下），kernel meta 里的 input 长度
// 用 2^64-1 占位表示"大小未知"，此时该入参数据无法 dump，需按空输入降级（见 ArgsContext.cpp DumpInputData）。
constexpr uint64_t UNKNOWN_MEM_SIZE = 0xFFFFFFFFFFFFFFFFULL;
constexpr uint64_t MAX_MEM_SIZE = 10ULL * 1024 * 1024 * 1024;

aclError ArgsCtxMallocHostStub(void **hostPtr, size_t size)
{
    *hostPtr = malloc(size);
    return ACL_ERROR_NONE;
}

aclError ArgsCtxFreeHostStub(void *hostPtr)
{
    free(hostPtr);
    return ACL_ERROR_NONE;
}

aclError ArgsCtxMemcpyStub(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind)
{
    // 单测里 src 是设备地址，不可直接访问，只把目标缓冲区填 0 模拟拷贝完成
    (void)destMax;
    (void)src;
    (void)kind;
    memset(dst, 0, count);
    return ACL_ERROR_NONE;
}

} // namespace [Dummy]

class ArgsContextTest : public testing::Test {
public:
    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

// 场景：没有运行期 tiling，meta 里 input 长度为占位值(2^64-1) -> 降级为空输入，且不应触发任何 device/host 内存拷贝
TEST_F(ArgsContextTest, test_DumpInputData_unknown_size_placeholder_expect_degrade_to_empty)
{
    DumperContext config;
    vector<AddrInfo> addrInfos{AddrInfo{0x120000200000ULL, UNKNOWN_MEM_SIZE, MemInfoSrc::EXTRA}};
    MOCKER(&aclrtMallocHostImplOrigin).expects(never());
    MOCKER(&aclrtMemcpyImplOrigin).expects(never());

    EXPECT_TRUE(DumpInputData("/tmp", addrInfos, config));
    ASSERT_EQ(config.inputSizeList.size(), 1UL);
    ASSERT_EQ(config.inputPathList.size(), 1UL);
    EXPECT_EQ(config.inputSizeList[0], 0UL);
    EXPECT_EQ(config.inputPathList[0], "n");
}

// 场景：length == 0（既有约定）-> 空输入，与占位值处理保持一致
TEST_F(ArgsContextTest, test_DumpInputData_empty_input_expect_degrade_to_empty)
{
    DumperContext config;
    vector<AddrInfo> addrInfos{AddrInfo{0, 0, MemInfoSrc::EXTRA}};
    MOCKER(&aclrtMemcpyImplOrigin).expects(never());

    EXPECT_TRUE(DumpInputData("/tmp", addrInfos, config));
    ASSERT_EQ(config.inputSizeList.size(), 1UL);
    ASSERT_EQ(config.inputPathList.size(), 1UL);
    EXPECT_EQ(config.inputSizeList[0], 0UL);
    EXPECT_EQ(config.inputPathList[0], "n");
}

// 场景：正常输入 + 占位值 + 空输入混合 -> size/path 列表与入参下标严格对齐（正常项仍走 dump 流程）
TEST_F(ArgsContextTest, test_DumpInputData_mixed_inputs_expect_lists_aligned)
{
    constexpr uint64_t normalSize = 128UL;
    DumperContext config;
    vector<AddrInfo> addrInfos{
        AddrInfo{0x120000200000ULL, normalSize, MemInfoSrc::EXTRA},
        AddrInfo{0, UNKNOWN_MEM_SIZE, MemInfoSrc::EXTRA},
        AddrInfo{0, 0, MemInfoSrc::EXTRA},
    };
    MOCKER(&aclrtMallocHostImplOrigin).stubs().will(invoke(ArgsCtxMallocHostStub));
    MOCKER(&aclrtFreeHostImplOrigin).stubs().will(invoke(ArgsCtxFreeHostStub));
    MOCKER(&aclrtMemcpyImplOrigin).stubs().will(invoke(ArgsCtxMemcpyStub));
    MOCKER(&WriteBinary).stubs().will(returnValue(size_t(normalSize)));  // 单测不落盘

    EXPECT_TRUE(DumpInputData("/tmp", addrInfos, config));
    ASSERT_EQ(config.inputSizeList.size(), 3UL);
    ASSERT_EQ(config.inputPathList.size(), 3UL);
    EXPECT_EQ(config.inputSizeList[0], normalSize);
    EXPECT_EQ(config.inputPathList[0], "/tmp/input_0.bin");
    EXPECT_EQ(config.inputSizeList[1], 0UL);
    EXPECT_EQ(config.inputPathList[1], "n");
    EXPECT_EQ(config.inputSizeList[2], 0UL);
    EXPECT_EQ(config.inputPathList[2], "n");
}

// 保护：既不是占位值、又超过 10G 上限 -> 仍然判定为异常输入（返回 false）
TEST_F(ArgsContextTest, test_DumpInputData_oversize_input_expect_false)
{
    DumperContext config;
    vector<AddrInfo> addrInfos{AddrInfo{0x120000200000ULL, MAX_MEM_SIZE + 1, MemInfoSrc::EXTRA}};
    MOCKER(&aclrtMallocHostImplOrigin).expects(never());

    EXPECT_FALSE(DumpInputData("/tmp", addrInfos, config));
}
