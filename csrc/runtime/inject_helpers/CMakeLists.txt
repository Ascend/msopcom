file(GLOB COMMON_SOURCE
    KernelContext.cpp
    MemoryContext.cpp
    KernelReplacement.cpp
    KernelMatcher.cpp
    ProfDataCollect.cpp
    ProfTask.cpp
    MsTx.cpp
    ProfConfig.cpp
    MemoryDataCollect.cpp
    DevMemManager.cpp
    DBITask.cpp
    ConfigManager.cpp
    LaunchArgs.cpp
    InstrReport.cpp
    BBCountDumper.cpp
    InteractHelper.cpp
    DFXKernelLauncher.cpp
    RegisterContext.cpp
    RegisterManager.cpp
    FuncContext.cpp
    FuncManager.cpp
    StubFuncContext.cpp
    TilingFuncContext.cpp
    ArgsManager.cpp
    ArgsContext.cpp
    ArgsHandleContext.cpp
    ArgsExContext.cpp
    ArgsRawContext.cpp
    LaunchManager.cpp
    LaunchContext.cpp
    DeviceContext.cpp
    KernelMetaDataParser.cpp
    LocalDevice.cpp
    LaunchInitFinalize.cpp
    MemGuard.cpp
)
list(SORT COMMON_SOURCE)
target_sources(msopprof_injection PUBLIC ${COMMON_SOURCE})
target_sources(mssanitizer_injection PUBLIC ${COMMON_SOURCE})
# injection_test_obj 后续需包括全量代码
target_sources(injection_test_obj PUBLIC ${COMMON_SOURCE})
