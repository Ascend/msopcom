cmake_minimum_required(VERSION 3.0)

project(msopscommon VERSION 0.1.0)
set(CMAKE_SKIP_RPATH TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(HOST_INJECTION_CSRC ${ROOT_DIR}/csrc ABSOLUTE)
get_filename_component(JSON_DIR ${ROOT_DIR}/thirdparty/json/single_include ABSOLUTE)
get_filename_component(INJECTION_THIRDPARTY_INCLUDE ${HOST_INJECTION_CSRC}/include/thirdparty ABSOLUTE)

# 获取版本信息
find_package(Git REQUIRED)
execute_process(
  COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  OUTPUT_VARIABLE FUNC_INJECTION_COMMIT_REVISION
  OUTPUT_STRIP_TRAILING_WHITESPACE)
add_definitions(-D__FUNC_INJECTION_COMMIT_REVISION__="${FUNC_INJECTION_COMMIT_REVISION}")

# 为了适用于工程本身构建，也适用于其它工程调用此工程，CMAKE_INSTALL_PREFIX的判断方式需要判断是否未设定或者系统默认路径
if (${CMAKE_INSTALL_PREFIX} STREQUAL "/usr/local" OR ${CMAKE_INSTALL_PREFIX} STREQUAL "/usr" OR NOT DEFINED CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX ${ROOT_DIR}/output CACHE STRING "install prefix" FORCE)
    message(STATUS "No install prefix selected, default to ${CMAKE_INSTALL_PREFIX}")
endif()

include(cmake/options.cmake)
option(BUILD_TESTS "build unit tests" OFF)
if (${BUILD_TESTS})
    set(CMAKE_CXX_FLAGS "-fprofile-arcs -ftest-coverage -fexceptions")
    add_definitions("-D__BUILD_TESTS__")
    include_directories(thirdparty/boost/libs/type_traits/include)
    include_directories(thirdparty/boost/libs/config/include)
    include_directories(thirdparty/boost/libs/typeof/include)
endif()

add_subdirectory(csrc)

if (${BUILD_TESTS})
    message(STATUS "Enable unit tests build")
    add_subdirectory(thirdparty)
    add_subdirectory(test)
endif()
