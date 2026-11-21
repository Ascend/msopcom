# 编译各种依赖

# runtime
# 新版 CANN 将 ACL 实现接口合入 libruntime，桩库也需提供这些接口。
add_library(runtime SHARED runtime.cpp acl_rt_impl.cpp)
add_library(acl_rt_impl SHARED acl_rt_impl.cpp)
add_library(ascendcl_impl SHARED acl_rt_impl.cpp)
add_library(ascend_hal SHARED ascend_hal.cpp)
include_directories(runtime ${HOST_INJECTION_CSRC} ${INJECTION_THIRDPARTY_INCLUDE})
include_directories(acl_rt_impl ${HOST_INJECTION_CSRC} ${INJECTION_THIRDPARTY_INCLUDE})
include_directories(ascend_hal ${HOST_INJECTION_CSRC} ${INJECTION_THIRDPARTY_INCLUDE})
target_link_libraries(runtime)
target_link_libraries(acl_rt_impl)
target_link_libraries(ascendcl_impl)
target_link_libraries(ascend_hal)

file(GLOB SOURCE
   FakeElf.cpp
)
list(SORT SOURCE)
target_sources(injection_test_obj PUBLIC ${SOURCE})
