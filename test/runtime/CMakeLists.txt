file(GLOB SOURCE
    RuntimeOriginTest.cpp
    SetDeviceTest.cpp
    MallocTest.cpp
    FreeTest.cpp
    MapMemTest.cpp
    UnmapMemTest.cpp
    MemsetTest.cpp
    MemcpyTest.cpp
    RegisterAllKernelTest.cpp
    KernelLaunchTest.cpp
    KernelLaunchWithFlagV2Test.cpp
    AicpuKernelLaunchWithArgsTest.cpp
    DevBinaryRegisterTest.cpp
    KernelLaunchWithHandleV2Test.cpp
    DevBinaryUnRegisterTest.cpp
    FunctionRegisterTest.cpp
    SetExceptionExtInfoTest.cpp
    GetSocVersionTest.cpp
    GetAiCoreCountTest.cpp
    StreamSynchronizeWithTimeoutTest.cpp

    HijackedFuncOfGetDeviceInfoTest.cpp
    HijackedFuncOfMapMemTest.cpp
    HijackedFuncOfIpcCloseMemory.cpp
    HijackedFuncOfIpcDestroyMemoryName.cpp
    HijackedFuncOfIpcOpenMemory.cpp
    HijackedFuncOfIpcSetMemoryName.cpp
    HijackedFuncOfGetL2CacheOffsetTest.cpp
    HijackedFuncOfCtxGetOverflowAddrTest.cpp
    
    SetDeviceExTest.cpp
    SetDeviceV2Test.cpp
    GetDeviceTest.cpp
    GetDeviceCountTest.cpp
    CtxCreateTest.cpp
    CtxCreateV2Test.cpp
    CtxCreateExTest.cpp
    DeviceResetTest.cpp
    DeviceStatusQueryTest.cpp

    injectors/KernelContextTest.cpp
    injectors/MemoryContextTest.cpp
    injectors/KernelReplacementTest.cpp
    injectors/ProfDataCollectTest.cpp
    injectors/ProfTaskTest.cpp
    injectors/MsTxTest.cpp
    injectors/ProfConfigTest.cpp
    injectors/MemoryDataCollectTest.cpp
    injectors/DevMemManagerTest.cpp
    injectors/InstrReportTest.cpp
    injectors/BBCountDumperTest.cpp
    injectors/KernelMatcherTest.cpp
    injectors/DBITaskTest.cpp
    injectors/ConfigManager.cpp
    injectors/CamodelTest.cpp
    injectors/DFXKernelLauncherTest.cpp
    injectors/MemGuardTest.cpp

    BinaryLoadFromFileTest.cpp
    BinaryLoadFromDataTest.cpp
    BinaryLoadTest.cpp
    RegisterManagerTest.cpp
    RegisterContextTest.cpp
    BinaryGetFunctionTest.cpp
    BinaryGetFunctionByEntryTest.cpp
    FuncManagerTest.cpp
    FuncContextTest.cpp
)
list(SORT SOURCE)
target_sources(injection_test_obj PUBLIC ${SOURCE})
