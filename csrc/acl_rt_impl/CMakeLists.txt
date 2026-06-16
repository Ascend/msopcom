# 如果所有都需要，则放入core中
file(GLOB INJECTION_SRC
    AscendclImplOrigin.cpp
    HijackedFuncOfAclrtSetDeviceImpl.cpp
    HijackedFuncOfAclrtResetDeviceImpl.cpp
    HijackedFuncOfAclrtMallocImpl.cpp
    HijackedFuncOfAclrtMallocCachedImpl.cpp
    HijackedFuncOfAclrtMallocHostImpl.cpp
    HijackedFuncOfAclrtMallocHostWithCfgImpl.cpp
    HijackedFuncOfAclrtFreeImpl.cpp
    HijackedFuncOfAclrtFreeHostImpl.cpp
    HijackedFuncOfAclrtMemsetImpl.cpp
    HijackedFuncOfAclrtMemsetAsyncImpl.cpp
    HijackedFuncOfAclrtMemcpyImpl.cpp
    HijackedFuncOfAclrtMemcpyAsyncImpl.cpp
    HijackedFuncOfAclrtMemcpy2dImpl.cpp
    HijackedFuncOfAclrtMemcpy2dAsyncImpl.cpp
    HijackedFuncOfAclrtMapMemImpl.cpp
    HijackedFuncOfAclrtUnmapMemImpl.cpp
    HijackedFuncOfAclrtIpcMemGetExportKeyImpl.cpp
    HijackedFuncOfAclrtIpcMemImportByKeyImpl.cpp
    HijackedFuncOfAclrtIpcMemCloseImpl.cpp
    HijackedFuncOfAclmdlRICaptureBeginImpl.cpp
    HijackedFuncOfAclmdlRICaptureEndImpl.cpp
    HijackedFuncOfAclmdlRIBindStreamImpl.cpp
    HijackedFuncOfAclmdlRIUnbindStreamImpl.cpp
    HijackedFuncOfAclrtBinaryLoadFromFileImpl.cpp
    HijackedFuncOfAclrtBinaryLoadImpl.cpp
    HijackedFuncOfAclrtBinaryLoadFromDataImpl.cpp
    HijackedFuncOfAclrtCreateBinaryImpl.cpp
    HijackedFuncOfAclrtBinaryGetFunctionImpl.cpp
    HijackedFuncOfAclrtBinaryGetFunctionByEntryImpl.cpp
    HijackedFuncOfAclrtRegisterCpuFuncImpl.cpp
    HijackedFuncOfAclrtKernelArgsInitImpl.cpp
    HijackedFuncOfAclrtKernelArgsAppendImpl.cpp
    HijackedFuncOfAclrtKernelArgsAppendPlaceHolderImpl.cpp
    HijackedFuncOfAclrtKernelArgsGetPlaceHolderBufferImpl.cpp
    HijackedFuncOfAclrtKernelArgsParaUpdateImpl.cpp
    HijackedFuncOfAclrtKernelArgsFinalizeImpl.cpp
    HijackedFuncOfAclrtGetDeviceImpl.cpp
    HijackedFuncOfAclrtCreateContextImpl.cpp
    HijackedFuncOfAclrtQueryDeviceStatusImpl.cpp
    HijackedFuncOfAclrtFreeImpl.cpp
    HijackedFuncOfAclrtGetDeviceCountImpl.cpp
    HijackedFuncOfAclrtGetDeviceInfoImpl.cpp
    HijackedFuncOfAclrtGetSocNameImpl.cpp
    HijackedFuncOfAclrtLaunchKernelImpl.cpp
    HijackedFuncOfAclrtLaunchKernelV2Impl.cpp
    HijackedFuncOfAclrtLaunchKernelWithConfigImpl.cpp
    HijackedFuncOfAclrtLaunchKernelWithHostArgsImpl.cpp
    HijackedFuncOfAclrtMallocWithCfgImpl.cpp
    HijackedFuncOfAclrtKernelArgsInitByUserMemImpl.cpp
    HijackedFuncOfAclrtGetFuncBySymbolImpl.cpp
)

list(SORT INJECTION_SRC)
# 测试需要包含所有的hijacked func
target_sources(injection_test_obj PUBLIC ${INJECTION_SRC})
target_sources(mssanitizer_injection PUBLIC ${INJECTION_SRC})
target_sources(msopprof_injection PUBLIC ${INJECTION_SRC})
