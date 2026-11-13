file(GLOB SOURCE
    PipeCall.cpp
    FileSystem.cpp
    ElfLoader.cpp
    Ustring.cpp
    Environment.cpp
    Path.cpp
)
list(SORT SOURCE)
target_sources(core_obj PUBLIC ${SOURCE})

# 设备属性查询依赖 Origin 包装，仅由注入目标使用，不引入公共 core_obj。
target_sources(injection_test_obj PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/DeviceAttr.cpp)
target_sources(msopprof_injection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/DeviceAttr.cpp)
target_sources(mssanitizer_injection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/DeviceAttr.cpp)
