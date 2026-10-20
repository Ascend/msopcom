file(GLOB SOURCE
        AdumpGetDFXInfoAddrForDynamicTest.cpp
        AdumpGetDFXInfoAddrForStaticTest.cpp
)
list(SORT SOURCE)
target_sources(injection_test_obj PUBLIC ${SOURCE})
