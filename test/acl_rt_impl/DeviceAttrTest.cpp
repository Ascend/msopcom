/* -------------------------------------------------------------------------
 *  This file is part of the MindStudio project.
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

#include <limits>
#include <vector>
#include <gtest/gtest.h>

#include "acl_rt_impl/AscendclImplOrigin.h"
#include "mockcpp/mockcpp.hpp"
#include "runtime/inject_helpers/DeviceAttr.h"

namespace {

struct AttributeQuery {
    uint32_t expectedDevice = 7;
    int32_t currentDevice = 3;
    aclError deviceStatus = ACL_SUCCESS;
    aclError attributeStatus = ACL_SUCCESS;
    int64_t npuArch = DeviceAttr::NPU_ARCH_C220;
    int64_t l2Bytes = int64_t{1} << 33;
    int64_t cores = std::numeric_limits<uint32_t>::max();
    std::vector<aclrtDevAttr> attributes;
} query;

aclError GetCurrentDevice(int32_t *device) {
    *device = query.currentDevice;
    return query.deviceStatus;
}

aclError GetAttribute(uint32_t device, aclrtDevAttr attr, int64_t *value) {
    EXPECT_EQ(query.expectedDevice, device);
    query.attributes.push_back(attr);
    if (attr == ACL_DEV_ATTR_NPU_ARCH) {
        *value = query.npuArch;
    } else if (attr == ACL_DEV_ATTR_L2_CACHE_SIZE) {
        *value = query.l2Bytes;
    } else {
        EXPECT_EQ(ACL_DEV_ATTR_AICORE_CORE_NUM, attr);
        *value = query.cores;
    }
    return query.attributeStatus;
}

class DeviceAttrTest : public testing::Test {
protected:
    void SetUp() override { query = AttributeQuery{}; }
    void TearDown() override { GlobalMockObject::verify(); }
};

TEST_F(DeviceAttrTest, explicit_device_queries_all_attributes_without_current_context) {
    constexpr uint32_t deviceId = 7;
    MOCKER(&aclrtGetDeviceImplOrigin).expects(never());
    MOCKER(&aclrtGetDeviceInfoImplOrigin).expects(exactly(5)).will(invoke(GetAttribute));

    uint64_t size = 0;
    uint32_t coreNum = 0;
    int64_t arch = 0;
    EXPECT_TRUE(DeviceAttr::QueryNpuArch(deviceId, arch));
    EXPECT_EQ(DeviceAttr::NPU_ARCH_C220, arch);
    EXPECT_TRUE(DeviceAttr::QueryL2CacheSize(deviceId, size));
    EXPECT_EQ(static_cast<uint64_t>(query.l2Bytes), size);
    EXPECT_TRUE(DeviceAttr::QueryAicoreCoreNum(deviceId, coreNum));
    EXPECT_EQ(std::numeric_limits<uint32_t>::max(), coreNum);
    query.npuArch = DeviceAttr::NPU_ARCH_C310;
    EXPECT_TRUE(DeviceAttr::QueryNpuArch(deviceId, arch));
    EXPECT_EQ(DeviceAttr::NPU_ARCH_C310, arch);
    query.cores = int64_t{1} << 32;
    EXPECT_TRUE(DeviceAttr::QueryAicoreCoreNum(deviceId, coreNum));
    EXPECT_EQ(static_cast<uint32_t>(query.cores), coreNum);
    EXPECT_EQ((std::vector<aclrtDevAttr>{ACL_DEV_ATTR_NPU_ARCH, ACL_DEV_ATTR_L2_CACHE_SIZE,
                  ACL_DEV_ATTR_AICORE_CORE_NUM, ACL_DEV_ATTR_NPU_ARCH, ACL_DEV_ATTR_AICORE_CORE_NUM}),
        query.attributes);
}

TEST_F(DeviceAttrTest, negative_device_queries_current_device_then_its_attributes) {
    query.expectedDevice = 3;
    query.npuArch = DeviceAttr::NPU_ARCH_C310;
    query.l2Bytes = 96 * 1024 * 1024;
    query.cores = 20;
    MOCKER(&aclrtGetDeviceImplOrigin).expects(exactly(3)).will(invoke(GetCurrentDevice));
    MOCKER(&aclrtGetDeviceInfoImplOrigin).expects(exactly(3)).will(invoke(GetAttribute));

    uint64_t size = 0;
    uint32_t coreNum = 0;
    int64_t arch = 0;
    EXPECT_TRUE(DeviceAttr::QueryNpuArch(-1, arch));
    EXPECT_EQ(DeviceAttr::NPU_ARCH_C310, arch);
    EXPECT_TRUE(DeviceAttr::QueryL2CacheSize(-1, size));
    EXPECT_EQ(static_cast<uint64_t>(query.l2Bytes), size);
    EXPECT_TRUE(DeviceAttr::QueryAicoreCoreNum(-1, coreNum));
    EXPECT_EQ(20U, coreNum);
    EXPECT_EQ(
        (std::vector<aclrtDevAttr>{ACL_DEV_ATTR_NPU_ARCH, ACL_DEV_ATTR_L2_CACHE_SIZE, ACL_DEV_ATTR_AICORE_CORE_NUM}),
        query.attributes);
}

TEST_F(DeviceAttrTest, invalid_current_device_fails_all_queries_without_attribute_lookup) {
    struct TestCase {
        aclError status;
        int32_t device;
    };
    for (const auto &testCase : {TestCase{ACL_ERROR_INTERNAL_ERROR, 3}, TestCase{ACL_SUCCESS, -1}}) {
        SCOPED_TRACE(testCase.status);
        SCOPED_TRACE(testCase.device);
        query.currentDevice = testCase.device;
        query.deviceStatus = testCase.status;
        MOCKER(&aclrtGetDeviceImplOrigin).expects(exactly(3)).will(invoke(GetCurrentDevice));
        MOCKER(&aclrtGetDeviceInfoImplOrigin).expects(never());
        uint64_t size = 123;
        uint32_t coreNum = 45;
        int64_t arch = 67;
        EXPECT_FALSE(DeviceAttr::QueryNpuArch(-1, arch));
        EXPECT_FALSE(DeviceAttr::QueryL2CacheSize(-1, size));
        EXPECT_FALSE(DeviceAttr::QueryAicoreCoreNum(-1, coreNum));
        EXPECT_EQ(67, arch);
        EXPECT_EQ(123U, size);
        EXPECT_EQ(45U, coreNum);
        EXPECT_TRUE(query.attributes.empty());
        GlobalMockObject::verify();
        query.attributes.clear();
    }
}

TEST_F(DeviceAttrTest, failed_or_invalid_attributes_preserve_outputs) {
    struct TestCase {
        aclrtDevAttr attr;
        int64_t value;
        aclError status;
    };
    const TestCase cases[] = {
        {ACL_DEV_ATTR_L2_CACHE_SIZE, 0, ACL_SUCCESS},
        {ACL_DEV_ATTR_L2_CACHE_SIZE, -1, ACL_SUCCESS},
        {ACL_DEV_ATTR_L2_CACHE_SIZE, 1024, ACL_ERROR_INTERNAL_ERROR},
        {ACL_DEV_ATTR_NPU_ARCH, 0, ACL_SUCCESS},
        {ACL_DEV_ATTR_NPU_ARCH, -1, ACL_SUCCESS},
        {ACL_DEV_ATTR_NPU_ARCH, DeviceAttr::NPU_ARCH_C220, ACL_ERROR_INTERNAL_ERROR},
        {ACL_DEV_ATTR_AICORE_CORE_NUM, 0, ACL_SUCCESS},
        {ACL_DEV_ATTR_AICORE_CORE_NUM, -1, ACL_SUCCESS},
        {ACL_DEV_ATTR_AICORE_CORE_NUM, 20, ACL_ERROR_INTERNAL_ERROR},
    };
    for (const auto &testCase : cases) {
        SCOPED_TRACE(testCase.attr);
        SCOPED_TRACE(testCase.value);
        SCOPED_TRACE(testCase.status);
        query.l2Bytes = testCase.value;
        query.cores = testCase.value;
        query.npuArch = testCase.value;
        query.attributeStatus = testCase.status;
        query.attributes.clear();
        MOCKER(&aclrtGetDeviceImplOrigin).expects(never());
        MOCKER(&aclrtGetDeviceInfoImplOrigin).expects(once()).will(invoke(GetAttribute));
        uint64_t size = 123;
        uint32_t coreNum = 45;
        int64_t arch = 67;
        if (testCase.attr == ACL_DEV_ATTR_L2_CACHE_SIZE) {
            EXPECT_FALSE(DeviceAttr::QueryL2CacheSize(7, size));
            EXPECT_EQ(123U, size);
        } else if (testCase.attr == ACL_DEV_ATTR_NPU_ARCH) {
            EXPECT_FALSE(DeviceAttr::QueryNpuArch(7, arch));
            EXPECT_EQ(67, arch);
        } else {
            EXPECT_FALSE(DeviceAttr::QueryAicoreCoreNum(7, coreNum));
            EXPECT_EQ(45U, coreNum);
        }
        EXPECT_EQ((std::vector<aclrtDevAttr>{testCase.attr}), query.attributes);
        GlobalMockObject::verify();
    }
}

} // namespace
