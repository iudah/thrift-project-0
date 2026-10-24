cmake_minimum_required(VERSION 3.20)

project(Thrift-Project VERSION 1.1.2)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_SANITIZERS "Enable Address and Undefined Behaviours Sanitizers" OFF)

message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "System Processor: ${CMAKE_SYSTEM_PROCESSOR}")

include(ExternalProject)
include(CheckCCompilerFlag)

configure_file(Thrift-Project.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Thrift-Project.h)

add_subdirectory(vendor)

include(headers.cmake)
include(sources.cmake)
add_executable(thrift ${SOURCE_FILES})
target_sources(thrift PRIVATE ${SOURCE_FILES} ${HEADER_FILES})


#target_include_directories(thrift PRIVATE ${PROJECT_BINARY_DIR})
file(REAL_PATH "${CMAKE_CURRENT_BINARY_DIR}/" TRUE_BINARY_DIR)
target_include_directories(thrift
    PUBLIC
    $<BUILD_INTERFACE:include >
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/libsodium/src/libsodium/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/thrift_crypto>
    $<BUILD_INTERFACE:${TRUE_BINARY_DIR}/generated>
    $<BUILD_INTERFACE:${TRUE_BINARY_DIR}/vendor/XKCP_out/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(thrift PRIVATE memalloc)
target_link_libraries(thrift PRIVATE thrift_crypto)
target_link_libraries(thrift PRIVATE zot)
target_link_libraries(thrift PRIVATE ison)
target_link_libraries(thrift PRIVATE sodium)
target_link_libraries(thrift PRIVATE blake3)
target_link_libraries(thrift PRIVATE XKCP)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  if(MSVC)
    target_compile_options(thrift PRIVATE /Zi /Od /RTC1)
    target_link_options(thrift PRIVATE /DEBUG)
  else()
    target_compile_options(thrift PRIVATE -g -O0 -fno-omit-frame-pointer)
  endif()
  if(ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(thrift PRIVATE -fsanitize=address -fsanitize=undefined)
    target_link_options(thrift PRIVATE -fsanitize=address -fsanitize=undefined)
  endif()
endif()

if((CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64") AND NOT MSVC)
  check_c_compiler_flag("-mfpu=neon" HAS_NEON_FLAG)
  if(HAS_NEON_FLAG)
    target_compile_options(thrift PRIVATE -mfpu=neon)
  endif()
endif()

find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

Python3_add_library(thriftproject MODULE
    pylib/py_api.c
    src/book.c
    src/book_io.c
    src/book_json.c
    src/book_crypto.c
    src/wallet.c
    src/get_password.c
    src/router.c
    src/node_net.c
    src/http_handlers.c
)

target_include_directories(thriftproject PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/vendor/thrift_crypto
    $<BUILD_INTERFACE:${TRUE_BINARY_DIR}/generated>
)

target_compile_options(thriftproject PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fPIC>
)
target_link_libraries(thriftproject PRIVATE
    ison memalloc zot thrift_crypto XKCP sodium blake3 pthread m
)

# if(BUILD_TESTS)
#     include(include_google_test.cmake)
#     add_executable(ison_test tests/isontest.cpp tests/isonsubclass.cpp)
#     target_link_libraries(ison_test PRIVATE thrift gtest gtest_main)
#     enable_testing()

#     include(GoogleTest)
#     gtest_discover_tests(ison_test)
# endif()

include(GNUInstallDirs)

install(TARGETS thrift
    EXPORT IsonTargets
    DESTINATION ${CMAKE_INSTALL_BINDIR})

