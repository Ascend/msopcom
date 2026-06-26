# 如果所有都需要，则放入core中
file(GLOB INJECTION_SRC
    RuntimeOrigin.cpp
    CaRuntimeOrigin.cpp
    HijackedFuncOfSetDevice.cpp
    HijackedFuncOfMalloc.cpp
    HijackedFuncOfFree.cpp
    HijackedFuncOfMapMem.cpp
    HijackedFuncOfUnmapMem.cpp
    HijackedFuncOfMemset.cpp
    HijackedFuncOfMemcpy.cpp
    HijackedFuncOfKernelLaunch.cpp
    HijackedFuncOfFunctionRegister.cpp
    HijackedFuncOfRegisterAllKernel.cpp
    HijackedFuncOfDevBinaryRegister.cpp
    HijackedFuncOfDevBinaryUnRegister.cpp
    HijackedFuncOfSetExceptionExtInfo.cpp
    HijackedFuncOfKernelLaunchWithFlagV2.cpp
    HijackedFuncOfKernelLaunchWithHandleV2.cpp
    HijackedFuncOfSetDevice.cpp
    HijackedFuncOfSetDeviceV2.cpp
    HijackedFuncOfSetDeviceEx.cpp
    HijackedFuncOfGetDevice.cpp
    HijackedFuncOfGetDeviceCount.cpp
    HijackedFuncOfCtxCreate.cpp
    HijackedFuncOfCtxCreateV2.cpp
    HijackedFuncOfCtxCreateEx.cpp
    HijackedFuncOfDeviceReset.cpp
    HijackedFuncOfDeviceResetEx.cpp
    HijackedFuncOfDeviceStatusQuery.cpp
    HijackedFuncOfGetSocVersion.cpp
    HijackedFuncOfGetAiCoreCount.cpp
    HijackedFuncOfAiCpuKernelLaunchExWithArgs.cpp
    HijackedFuncOfStreamSynchronizeWithTimeout.cpp
    HijackedFuncOfGetDeviceInfo.cpp
    HijackedFuncOfIpcSetMemoryName.cpp
    HijackedFuncOfIpcDestroyMemoryName.cpp
    HijackedFuncOfIpcOpenMemory.cpp
    HijackedFuncOfIpcCloseMemory.cpp
    HijackedFuncOfModelBindStream.cpp
    HijackedFuncOfGetL2CacheOffset.cpp
    HijackedFuncOfCtxGetOverflowAddr.cpp
)

add_subdirectory(inject_helpers)

list(SORT INJECTION_SRC)
# 测试需要包含所有的hijacked func
target_sources(injection_test_obj PUBLIC ${INJECTION_SRC})
target_sources(mssanitizer_injection PUBLIC ${INJECTION_SRC})
target_sources(msopprof_injection PUBLIC ${INJECTION_SRC})
