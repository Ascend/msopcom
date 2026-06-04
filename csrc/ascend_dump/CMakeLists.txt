# 如果所有都需要，则放入core中
file(GLOB INJECTION_SRC
        HijackedFuncOfAdumpGetDFXInfoAddrForDynamic.cpp
        )

list(SORT INJECTION_SRC)
# 测试需要包含所有的hijacked func
target_sources(injection_test_obj PUBLIC ${INJECTION_SRC})
target_sources(msopprof_injection PUBLIC ${INJECTION_SRC})
target_sources(mssanitizer_injection PUBLIC ${INJECTION_SRC})
