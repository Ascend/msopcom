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


#include <gtest/gtest.h>
#include "acl_rt_impl/HijackedFunc.h"
#include "ascend_hal/AscendHalOrigin.h"
#include "runtime/HijackedFunc.h"
#include "core/FuncSelector.h"
#include "runtime/inject_helpers/LocalDevice.h"
#include "utils/Serialize.h"
#include "mockcpp/mockcpp.hpp"

namespace {
std::vector<HostMemRecord> copyRecords;
void *const host = reinterpret_cast<void *>(0x1000);
void *const device = reinterpret_cast<void *>(0x2000);
void *const unknown = reinterpret_cast<void *>(0x3000);

class SynchronousAclCopy : public HijackedFuncOfAclrtMemcpyImpl {
public:
    explicit SynchronousAclCopy(FuncType origin) { originfunc_ = origin; }
};

ssize_t CaptureCopyRecord(LocalProcess *, std::string packet) {
    HostMemRecord record{};
    EXPECT_TRUE(Deserialize(packet.substr(sizeof(PacketHead)), record));
    copyRecords.push_back(record);
    return packet.size();
}

drvError_t QueryCopyEndpoint(DVdeviceptr addr, DVattribute *attr) {
    if (addr == unknown) {
        return static_cast<drvError_t>(1);
    }
    if (addr == device) {
        attr->memType = DV_MEM_LOCK_DEV;
    }
    return DRV_ERROR_NONE;
}
} // namespace

class DeviceMemcpy : public testing::Test {
protected:
    void SetUp() override {
        copyRecords.clear();
        MOCKER(IsSanitizer).stubs().will(returnValue(true));
        MOCKER(&LocalDevice::Notify).stubs().will(invoke(CaptureCopyRecord));
    }
    void TearDown() override { GlobalMockObject::verify(); }
    void Expect(size_t index, MemOpType type, uint64_t addr, MemInfoSrc source) {
        ASSERT_LT(index, copyRecords.size());
        EXPECT_EQ(copyRecords[index].type, type);
        EXPECT_EQ(copyRecords[index].dstAddr, addr);
        EXPECT_EQ(copyRecords[index].infoSrc, source);
        EXPECT_EQ(copyRecords[index].memSize, 16);
    }
};

TEST_F(DeviceMemcpy, AclAutomaticDirectionClassifiesBothEndpoints) {
    MOCKER(drvMemGetAttributeOrigin).expects(exactly(8)).will(invoke(QueryCopyEndpoint));
    HijackedFuncOfAclrtMemcpyImpl copy;
    copy.Pre(device, 16, host, 16, ACL_MEMCPY_DEFAULT);
    copy.Pre(host, 16, device, 16, ACL_MEMCPY_DEFAULT);
    copy.Pre(device, 16, device, 16, ACL_MEMCPY_DEFAULT);
    copy.Pre(host, 16, host, 16, ACL_MEMCPY_DEFAULT);
    ASSERT_EQ(copyRecords.size(), 4);
    Expect(0, MemOpType::STORE, 0x2000, MemInfoSrc::ACL);
    Expect(1, MemOpType::LOAD, 0x2000, MemInfoSrc::ACL);
    Expect(2, MemOpType::STORE, 0x2000, MemInfoSrc::ACL);
    Expect(3, MemOpType::LOAD, 0x2000, MemInfoSrc::ACL);
}

TEST_F(DeviceMemcpy, AutomaticDirectionSkipsReportingIfEitherQueryFails) {
    MOCKER(drvMemGetAttributeOrigin).expects(exactly(6)).will(invoke(QueryCopyEndpoint));
    HijackedFuncOfAclrtMemcpyImpl acl;
    acl.Pre(device, 16, unknown, 16, ACL_MEMCPY_DEFAULT);
    acl.Pre(unknown, 16, device, 16, ACL_MEMCPY_DEFAULT);
    acl.Pre(unknown, 16, unknown, 16, ACL_MEMCPY_DEFAULT);
    HijackedFuncOfMemcpy rt;
    rt.Pre(device, 16, unknown, 16, RT_MEMCPY_DEFAULT);
    EXPECT_TRUE(copyRecords.empty());
}

TEST_F(DeviceMemcpy, SynchronousAclSkipsEventsForUnsupportedDirections) {
    MOCKER(drvMemGetAttributeOrigin).expects(never());
    SynchronousAclCopy copy(
        +[](void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind) -> aclError {
            EXPECT_EQ(dst, device);
            EXPECT_EQ(src, unknown);
            EXPECT_EQ(destMax, 32);
            EXPECT_EQ(count, 16);
            if (kind == ACL_MEMCPY_DEVICE_TO_DEVICE) {
                EXPECT_EQ(copyRecords.size(), 2);
                return ACL_SUCCESS;
            }
            EXPECT_TRUE(copyRecords.empty());
            return kind == ACL_MEMCPY_INNER_DEVICE_TO_DEVICE ? 100001 : 100002;
        });
    EXPECT_EQ(copy.Call(device, 32, unknown, 16, ACL_MEMCPY_INNER_DEVICE_TO_DEVICE), 100001);
    EXPECT_EQ(copy.Call(device, 32, unknown, 16, ACL_MEMCPY_INTER_DEVICE_TO_DEVICE), 100002);
    EXPECT_TRUE(copyRecords.empty());
    EXPECT_EQ(copy.Call(device, 32, unknown, 16, ACL_MEMCPY_DEVICE_TO_DEVICE), ACL_SUCCESS);
    ASSERT_EQ(copyRecords.size(), 2);
    Expect(0, MemOpType::STORE, 0x2000, MemInfoSrc::ACL);
    Expect(1, MemOpType::LOAD, 0x3000, MemInfoSrc::ACL);
}

TEST_F(DeviceMemcpy, ExplicitDirectionsSkipAttributeQueries) {
    MOCKER(drvMemGetAttributeOrigin).expects(never());
    HijackedFuncOfAclrtMemcpyImpl copy;
    copy.Pre(device, 16, host, 16, ACL_MEMCPY_HOST_TO_BUF_TO_DEVICE);
    HijackedFuncOfMemcpy rt;
    rt.Pre(device, 16, host, 16, RT_MEMCPY_HOST_TO_DEVICE_EX);
    rt.Pre(host, 16, device, 16, RT_MEMCPY_DEVICE_TO_HOST_EX);
    ASSERT_EQ(copyRecords.size(), 3);
    Expect(0, MemOpType::STORE, 0x2000, MemInfoSrc::ACL);
    Expect(1, MemOpType::STORE, 0x2000, MemInfoSrc::RT);
    Expect(2, MemOpType::LOAD, 0x2000, MemInfoSrc::RT);
}

TEST_F(DeviceMemcpy, AsyncAndTwoDimensionalAclSkipInnerAndInterDirections) {
    MOCKER(drvMemGetAttributeOrigin).expects(never());
    HijackedFuncOfAclrtMemcpyAsyncImpl async;
    HijackedFuncOfAclrtMemcpy2dImpl sync2d;
    HijackedFuncOfAclrtMemcpy2dAsyncImpl async2d;
    for (auto kind : {ACL_MEMCPY_INNER_DEVICE_TO_DEVICE, ACL_MEMCPY_INTER_DEVICE_TO_DEVICE}) {
        async.Pre(device, 16, unknown, 16, kind, nullptr);
        sync2d.Pre(device, 32, unknown, 48, 16, 2, kind);
        async2d.Pre(device, 32, unknown, 48, 16, 2, kind, nullptr);
    }
    EXPECT_TRUE(copyRecords.empty());
}

TEST_F(DeviceMemcpy, TwoDimensionalAndRuntimeAutomaticCopiesUseDeviceEndpoints) {
    MOCKER(drvMemGetAttributeOrigin).expects(exactly(6)).will(invoke(QueryCopyEndpoint));
    HijackedFuncOfAclrtMemcpy2dImpl sync;
    sync.Pre(device, 32, host, 48, 16, 2, ACL_MEMCPY_DEFAULT);
    HijackedFuncOfAclrtMemcpy2dAsyncImpl async;
    async.Pre(host, 32, device, 48, 16, 2, ACL_MEMCPY_DEFAULT, nullptr);
    HijackedFuncOfMemcpy rt;
    rt.Pre(host, 16, device, 16, RT_MEMCPY_DEFAULT);
    ASSERT_EQ(copyRecords.size(), 5);
    Expect(0, MemOpType::STORE, 0x2000, MemInfoSrc::ACL);
    Expect(1, MemOpType::STORE, 0x2020, MemInfoSrc::ACL);
    Expect(2, MemOpType::LOAD, 0x2000, MemInfoSrc::ACL);
    Expect(3, MemOpType::LOAD, 0x2030, MemInfoSrc::ACL);
    Expect(4, MemOpType::LOAD, 0x2000, MemInfoSrc::RT);
}
