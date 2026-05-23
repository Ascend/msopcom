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

#include "acl.h"
// 本文件用于构造一个空实现的runtime库，使kernel-launcher编译构建时完成对libacendcl.so的链接

aclError aclrtSetDevice(int32_t deviceId)
{
    (void)deviceId;
    return ACL_SUCCESS;
}

aclError aclrtResetDevice(int32_t devId)
{
    (void)devId;
    return ACL_SUCCESS;
}

aclError aclrtMallocHost(void **hostPtr, size_t size)
{
    (void)hostPtr;
    (void)size;
    return ACL_SUCCESS;
}
aclError aclrtFreeHost(void *hostPtr)
{
    (void)hostPtr;
    return ACL_SUCCESS;
}

aclError aclrtMalloc(void **devPtr, size_t size, aclrtMemMallocPolicy policy)
{
    (void)devPtr;
    (void)size;
    (void)policy;
    return ACL_SUCCESS;
}

aclError aclrtFree(void *devPtr)
{
    (void)devPtr;
    return ACL_SUCCESS;
}

aclError aclrtMemcpy(void *dst, size_t destMax, const void *src, size_t count, aclrtMemcpyKind kind)
{
    (void)dst;
    (void)destMax;
    (void)src;
    (void)count;
    (void)kind;
    return ACL_SUCCESS;
}

aclError aclrtCreateStream(aclrtStream *stream)
{
    (void)stream;
    return ACL_SUCCESS;
}

aclError aclrtDestroyStream(aclrtStream stream)
{
    (void)stream;
    return ACL_SUCCESS;
}

aclError aclrtBinaryLoadFromFile(const char* binPath, aclrtBinaryLoadOptions *options, aclrtBinHandle *binHandle)
{
    (void)binPath;
    (void)options;
    (void)binHandle;
    return ACL_SUCCESS;
}

aclError aclrtLaunchKernelWithConfig(aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream,
                                     aclrtLaunchKernelCfg *cfg, aclrtArgsHandle argsHandle, void *reserve)
{
    (void)funcHandle;
    (void)blockDim;
    (void)stream;
    (void)cfg;
    (void)argsHandle;
    (void)reserve;
    return ACL_SUCCESS;
}

aclError aclrtKernelArgsAppend(aclrtArgsHandle argsHandle, void *param, size_t paramSize, aclrtParamHandle *paramHandle)
{
    (void)argsHandle;
    (void)param;
    (void)paramSize;
    (void)paramHandle;
    return ACL_SUCCESS;
}

aclError aclrtKernelArgsFinalize(aclrtArgsHandle argsHandle)
{
    (void)argsHandle;
    return ACL_SUCCESS;
}

aclrtBinary aclrtCreateBinary(const void *data, size_t dataLen)
{
    (void)data;
    (void)dataLen;
    return nullptr;
}

aclError aclrtBinaryGetFunction(const aclrtBinHandle binHandle, const char *kernelName, aclrtFuncHandle *funcHandle)
{
    (void)binHandle;
    (void)kernelName;
    (void)funcHandle;
    return ACL_SUCCESS;
}

aclError aclrtBinaryGetFunctionByEntry(aclrtBinHandle binHandle, uint64_t funcEntry, aclrtFuncHandle *funcHandle)
{
    (void)binHandle;
    (void)funcEntry;
    (void)funcHandle;
    return ACL_SUCCESS;
}

aclError aclrtRegisterCpuFuncImpl(const aclrtBinHandle binHandle, const char *funcName, const char *kernelName, aclrtFuncHandle *funcHandle)
{
    (void)binHandle;
    (void)funcName;
    (void)kernelName;
    (void)funcHandle;
    return ACL_SUCCESS;
}

aclError aclrtKernelArgsInit(aclrtFuncHandle funcHandle, aclrtArgsHandle *argsHandle)
{
    (void)argsHandle;
    (void)funcHandle;
    return ACL_SUCCESS;
}

aclError aclrtBinaryUnLoad(aclrtBinHandle binHandle)
{
    (void)binHandle;
    return ACL_SUCCESS;
}

aclError aclrtSynchronizeStream(aclrtStream stream)
{
    (void)stream;
    return ACL_SUCCESS;
}
