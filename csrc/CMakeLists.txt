# Mockcpp 组件有一堆编译告警，因此只有在不构建 UT 的时候强制拦截业务代码中的所有编译告警
if(NOT ${BUILD_TESTS})
  add_compile_options("-Werror")
endif()

# core_obj 用于承载公共部分
# injection_test_obj 用于承载差异化插桩部分，给测试使用
add_library(core_obj OBJECT)
set_source_files_properties(utils/DeviceAttr.cpp PROPERTIES
    COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden"
)
add_library(injection_test_obj OBJECT)
add_library(msopprof_injection SHARED)
add_library(mssanitizer_injection SHARED)

# 检测 so 中所有依赖的符号是否都有定义，防止出现符号未定义的错误
function(check_undefined_symbols SHARED_LIBRARY)
  target_link_options(${SHARED_LIBRARY} PRIVATE "-Wl,-z,defs")
endfunction()

check_undefined_symbols(msopprof_injection)
check_undefined_symbols(mssanitizer_injection)

add_subdirectory(bind)
add_subdirectory(core)
add_subdirectory(utils)
add_subdirectory(runtime)
add_subdirectory(ascend_hal)
add_subdirectory(ascend_dump)
add_subdirectory(ascendcl)
add_subdirectory(acl_rt_impl)
add_subdirectory(profapi)
add_subdirectory(kernel_injection)
add_subdirectory(hccl)
add_subdirectory(camodel)


target_include_directories(core_obj PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${JSON_DIR} ${INJECTION_THIRDPARTY_INCLUDE})
target_include_directories(injection_test_obj PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${JSON_DIR} kernel_injection/include ${INJECTION_THIRDPARTY_INCLUDE})
target_include_directories(msopprof_injection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${JSON_DIR} ${INJECTION_THIRDPARTY_INCLUDE})
target_include_directories(mssanitizer_injection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${JSON_DIR} ${INJECTION_THIRDPARTY_INCLUDE})
target_include_directories(msbit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${INJECTION_THIRDPARTY_INCLUDE})

add_dependencies(injection_test_obj core_obj)
add_dependencies(msopprof_injection core_obj)
add_dependencies(mssanitizer_injection core_obj)

target_link_libraries(core_obj PRIVATE
    stdc++fs
    pthread
    dl
)

target_link_libraries(mssanitizer_injection PRIVATE
    $<TARGET_OBJECTS:core_obj>
    stdc++fs
    pthread
    dl
)

target_link_libraries(msopprof_injection PRIVATE
    $<TARGET_OBJECTS:core_obj>
    stdc++fs
    pthread
    dl
)

install(TARGETS mssanitizer_injection msopprof_injection
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib64
)

add_subdirectory(tools)
