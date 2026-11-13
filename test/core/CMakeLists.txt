file(GLOB SOURCE
    BinaryInstrumentationTest.cpp
    CommunicationTest.cpp
    FuncSelectorTest.cpp
    FunctionLoaderTest.cpp
    LocalProcessTest.cpp
    LocalDeviceTest.cpp
    RemoteProcessTest.cpp
    DomainSocketTest.cpp
    HijackedLayerManagerTest.cpp
    PlatformConfigTest.cpp
)
list(SORT SOURCE)
target_sources(core_test_obj PRIVATE ${SOURCE})
