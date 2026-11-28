file(GLOB SOURCE
    HcclOriginTest.cpp
    HcclWindowCollectTest.cpp
    HcclCommInitClusterInfoTest.cpp
    HcclCommInitClusterInfoConfigTest.cpp
    HcclCommInitRootInfoConfigTest.cpp
    HcclCommInitRootInfoTest.cpp
)
list(SORT SOURCE)
target_sources(injection_test_obj PUBLIC ${SOURCE})
