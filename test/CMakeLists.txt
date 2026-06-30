# set header
get_filename_component(gtest_header ${ROOT_DIR}/thirdparty/googletest/googletest/include ABSOLUTE)
get_filename_component(mockcpp_header ${ROOT_DIR}/thirdparty/mockcpp/include ABSOLUTE)
get_filename_component(mockcpp_header_3rdparty ${ROOT_DIR}/thirdparty/mockcpp/3rdparty ABSOLUTE)
get_filename_component(boost_header ${ROOT_DIR}/thirdparty/boost ABSOLUTE)
get_filename_component(test_stub_header ${ROOT_DIR}/test ABSOLUTE)
get_filename_component(kernel_launcher_header ${HOST_INJECTION_CSRC}/tools/kernel_launcher ABSOLUTE)


add_compile_options("-fno-inline")
# 编译依赖
add_subdirectory(stub)

# UT
# 对于测试这边，公共用例编译到core_test_obj中
# 针对特有的用例，由对应的obj负责
# 现在coverage的范围和core一样大
add_library(core_test_obj OBJECT
        tools/kernel_launcher/LauncherTest.cpp)
add_executable(injectionTest main.cpp)

add_subdirectory(core)
add_subdirectory(utils)
add_subdirectory(runtime)
add_subdirectory(acl_rt_impl)
add_subdirectory(ascendcl)
add_subdirectory(ascend_dump)
add_subdirectory(ascend_hal)
add_subdirectory(hccl)
add_subdirectory(profapi)
add_subdirectory(kernel_injection)
add_subdirectory(tools)

target_include_directories(injectionTest PUBLIC ${HOST_INJECTION_CSRC} ${gtest_header} ${mockcpp_header} ${INJECTION_THIRDPARTY_INCLUDE} ${mockcpp_header_3rdparty})
target_include_directories(
    core_test_obj PUBLIC
    ${HOST_INJECTION_CSRC}
    ${gtest_header}
    ${mockcpp_header}
    ${mockcpp_header_3rdparty}
    ${boost_header}
    ${test_stub_header}
    ${kernel_launcher_header}
    ${JSON_DIR}
    ${INJECTION_THIRDPARTY_INCLUDE}
)

target_include_directories(injection_test_obj PUBLIC ${HOST_INJECTION_CSRC} ${gtest_header} ${mockcpp_header} ${boost_header} ${test_stub_header} ${INJECTION_THIRDPARTY_INCLUDE}
${mockcpp_header_3rdparty})


add_dependencies(injectionTest core_test_obj)
add_dependencies(core_test_obj injection_test_obj)
add_dependencies(core_test_obj core_obj)
add_dependencies(core_test_obj gtest)
add_dependencies(core_test_obj kernel_launcher_obj)
add_dependencies(core_test_obj mockcpp)
add_dependencies(injection_test_obj mockcpp)

target_link_libraries(
    injectionTest PRIVATE
    $<TARGET_OBJECTS:core_test_obj>
    $<TARGET_OBJECTS:injection_test_obj>
    $<TARGET_OBJECTS:core_obj>
    $<TARGET_OBJECTS:kernel_launcher_obj>
    $<TARGET_OBJECTS:kernel_launcher_acl>
    gtest
    dl
    gcov
    mockcpp
    runtime
    acl_rt_impl
    ascendcl_impl
    ascend_hal
    stdc++fs
)
