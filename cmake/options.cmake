
# 安全规范编译命令
add_compile_options("-fPIE")
add_compile_options("-std=c++11")
add_compile_options("-fPIC")
add_compile_options("-fstack-protector-all")
add_compile_options("-Wall")

add_compile_options("-fno-strict-aliasing")
# 定义 __FILENAME__ 宏，编译期提取纯文件名，替代 __FILE__ 用于日志
add_definitions("-D__FILENAME__=(strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)")

# 根据CMAKE_BUILD_TYPE选择编译选项（从父项目继承）
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options("-O0")
else()
    add_compile_options("-O2")
    add_compile_options("-D_FORTIFY_SOURCE=2")
endif()

if(NOT CMAKE_VERSION VERSION_LESS "3.13")
    add_link_options ("-Wl,-z,now")
    add_link_options ("-Wl,-z,relro")
    add_link_options ("-Wl,-z,noexecstack")
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_link_options ("-s")  # strip symbols
    endif()
endif()
