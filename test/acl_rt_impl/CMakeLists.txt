file(GLOB SOURCE
    AclRuntimeConfigTest.cpp
    AscendclImplOriginTest.cpp
    HijackedFuncOfKernelArgsInitTest.cpp
    HijackedFuncOfKernelArgsAppendTest.cpp
    HijackedFuncOfKernelArgsAppendPlaceHolderTest.cpp
    HijackedFuncOfKernelArgsGetPlaceHolderBufferTest.cpp
    HijackedFuncOfKernelArgsGetParaUpdateTest.cpp
    HijackedFuncOfKernelArgsGetFinalizeTest.cpp
    HijackedFuncOfAclrtLaunchKernelWithConfigTest.cpp
    HijackedFuncOfAclrtLaunchKernelTest.cpp
    HijackedFuncOfAclrtLaunchKernelV2Test.cpp
    HijackedFuncOfAclrtLaunchKernelWithHostArgsTest.cpp
    HijackedFuncOfAclrtLaunchSIMTKernelWithHostArgsTest.cpp
    HijackedFuncOfAclrtCreateContextImplTest.cpp
    HijackedFuncOfAclrtGetDeviceImplTest.cpp
    HijackedFuncOfAclrtGetDeviceCountImplTest.cpp
    HijackedFuncOfAclrtGetDeviceInfoImplTest.cpp
    HijackedFuncOfAclrtGetSocNameImplTest.cpp
    HijackedFuncOfAclrtQueryDeviceStatusImplTest.cpp
    HijackedFuncOfAclrtIpcMemCloseImplTest.cpp
    HijackedFuncOfAclrtIpcMemGetExportKeyTest.cpp
    HijackedFuncOfAclrtIpcMemImportByKeyTest.cpp
    HijackedFuncOfAclrtMallocWithCfgImplTest.cpp
    HijackedFuncOfAclrtMallocForTaskSchedulerImplTest.cpp
    HijackedFuncOfAclrtFreeWithDevSyncImplTest.cpp
    HijackFuncOfLaunchKernelTest.cpp
    HijackedFuncOfAclrtMallocImplTest.cpp
    HijackedFuncOfAclrtKernelArgsInitByUserMemImplTest.cpp
    HijackedFuncOfAclrtMapMemImplTest.cpp
    injectors/ArgsManagerTest.cpp
    injectors/ArgsHandleContextTest.cpp
    injectors/LaunchManagerTest.cpp
    injectors/LaunchContextTest.cpp
)
list(SORT SOURCE)
target_sources(injection_test_obj PUBLIC ${SOURCE})
