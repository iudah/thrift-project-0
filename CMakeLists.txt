cmake_minimum_required(VERSION 3.20)

project(Thrift-Project VERSION 1.1.1)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_SANITIZERS "Enable Address and Undefined Behaviours Sanitizers" OFF)

message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "System Processor: ${CMAKE_SYSTEM_PROCESSOR}")

include(ExternalProject)
include(CheckCCompilerFlag)


list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/memalloc/lib/cmake/memalloc"
    "${CMAKE_BINARY_DIR}/zot/lib/cmake/zot"
    "${CMAKE_BINARY_DIR}/ison/lib/cmake/ison"
    "${CMAKE_BINARY_DIR}/XKCP/lib/cmake/XKCP"
    "${CMAKE_BINARY_DIR}/blake3/lib/cmake/blake3"
    "${CMAKE_BINARY_DIR}/sodium/lib/cmake/sodium")

find_package(memalloc QUIET)
if(NOT TARGET memalloc::memalloc)
    message(STATUS "memalloc not found, adding as ExternalProject")
    ExternalProject_Add(
        MemAlloc
        GIT_REPOSITORY "https://github.com/iudah/memalloc.git"
        GIT_TAG origin/master
        GIT_SHALLOW TRUE
        INSTALL_DIR ${CMAKE_BINARY_DIR}/memalloc
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DBUILD_EXECUTABLE=OFF
        BUILD_ALWAYS OFF
        STAMP_DIR ${CMAKE_BINARY_DIR}/stamps
        UPDATE_DISCONNECTED TRUE
        BUILD_BYPRODUCTS "${CMAKE_BINARY_DIR}/memalloc/lib/${CMAKE_STATIC_LIBRARY_PREFIX}memalloc${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/memalloc/include")

    add_library(memalloc_imported STATIC IMPORTED)
    add_dependencies(memalloc_imported MemAlloc)
    set_target_properties(memalloc_imported PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/memalloc/lib/${CMAKE_STATIC_LIBRARY_PREFIX}memalloc${CMAKE_STATIC_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/memalloc/include"
    )
    add_library(memalloc::memalloc ALIAS memalloc_imported)
endif()

find_package(zot QUIET)
if(NOT TARGET zot::zot)
    message(STATUS "zot not found, adding as ExternalProject")
    ExternalProject_Add(
        zot
        GIT_REPOSITORY "https://github.com/iudah/zot.git"
        GIT_TAG origin/main
        GIT_SHALLOW TRUE
        INSTALL_DIR ${CMAKE_BINARY_DIR}/zot
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DBUILD_EXECUTABLE=OFF
        BUILD_ALWAYS OFF
        STAMP_DIR ${CMAKE_BINARY_DIR}/stamps
        UPDATE_DISCONNECTED TRUE
        BUILD_BYPRODUCTS "${CMAKE_BINARY_DIR}/zot/lib/${CMAKE_STATIC_LIBRARY_PREFIX}zot${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/zot/include")

    add_library(zot_imported STATIC IMPORTED)
    add_dependencies(zot_imported zot)
    set_target_properties(zot_imported PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/zot/lib/${CMAKE_STATIC_LIBRARY_PREFIX}zot${CMAKE_STATIC_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/zot/include"
    )
    add_library(zot::zot ALIAS zot_imported)
endif()

find_package(ison QUIET)
if(NOT TARGET ison::ison)
    message(STATUS "ison not found, adding as ExternalProject")
    ExternalProject_Add(
        ison
        GIT_REPOSITORY "https://github.com/iudah/ison.git"
        GIT_TAG origin/main
        GIT_SHALLOW TRUE
        INSTALL_DIR ${CMAKE_BINARY_DIR}/ison
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DBUILD_EXECUTABLE=OFF
        BUILD_ALWAYS OFF
        STAMP_DIR ${CMAKE_BINARY_DIR}/stamps
        UPDATE_DISCONNECTED TRUE
        BUILD_BYPRODUCTS "${CMAKE_BINARY_DIR}/ison/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ison${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/ison/include")

    add_library(ison_imported STATIC IMPORTED)
    add_dependencies(ison_imported ison)
    set_target_properties(ison_imported PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/ison/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ison${CMAKE_STATIC_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/ison/include"
    )
    add_library(ison::ison ALIAS ison_imported)
endif()


find_package(sodium QUIET)
if(NOT TARGET sodium::sodium)
    message(STATUS "sodium not found, adding as ExternalProject")
    ExternalProject_Add(
        sodium
        GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
        GIT_TAG stable
        GIT_SHALLOW TRUE
        BUILD_IN_SOURCE ON
        CONFIGURE_COMMAND ./configure --prefix=<INSTALL_DIR>
        BUILD_COMMAND make
        TEST_COMMAND make check
        INSTALL_COMMAND make install
        INSTALL_DIR ${CMAKE_BINARY_DIR}/sodium
        BUILD_BYPRODUCTS "${CMAKE_BINARY_DIR}/sodium/lib/${CMAKE_SHARED_LIBRARY_PREFIX}sodium${CMAKE_SHARED_LIBRARY_SUFFIX}"
        STAMP_DIR ${CMAKE_BINARY_DIR}/stamps
        UPDATE_DISCONNECTED TRUE
    )

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/sodium/include")

    add_library(sodium_imported SHARED IMPORTED)
    add_dependencies(sodium_imported sodium)
    set_target_properties(sodium_imported PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/sodium/lib/${CMAKE_SHARED_LIBRARY_PREFIX}sodium${CMAKE_SHARED_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/sodium/include"
    )
    add_library(sodium::sodium ALIAS sodium_imported)
endif()

find_package(blake3 QUIET)
if(NOT TARGET blake3::blake3)
    message(STATUS "blake3 not found, adding as ExternalProject")
    ExternalProject_Add(
        blake3
        GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3.git
        GIT_TAG 1.8.2
        GIT_SHALLOW TRUE
        INSTALL_DIR ${CMAKE_BINARY_DIR}/blake3
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        SOURCE_SUBDIR c
        BUILD_BYPRODUCTS "${CMAKE_BINARY_DIR}/blake3/lib/${CMAKE_STATIC_LIBRARY_PREFIX}blake3${CMAKE_STATIC_LIBRARY_SUFFIX}"
        STAMP_DIR ${CMAKE_BINARY_DIR}/stamps
        UPDATE_DISCONNECTED TRUE
    )

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/blake3/include")

    add_library(blake3_imported STATIC IMPORTED)
    add_dependencies(blake3_imported blake3)
    set_target_properties(blake3_imported PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/blake3/lib/${CMAKE_STATIC_LIBRARY_PREFIX}blake3${CMAKE_STATIC_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/blake3/include"
    )
    add_library(blake3::blake3 ALIAS blake3_imported)
endif()

find_package(XKCP QUIET)
if(NOT TARGET XKCP::XKCP)
    message(STATUS "XKCP not found, adding as ExternalProject")

    check_c_compiler_flag("-msse3" HAS_SSSE3)
    check_c_compiler_flag("-mavx" HAS_AVX)
    if(NOT HAS_AVX)
        check_c_compiler_flag("-xAVX" HAS_AVX)
    endif()
    check_c_compiler_flag("-mavx2" HAS_AVX2)
    if(NOT HAS_AVX2)
        check_c_compiler_flag("-xCORE-AVX2" HAS_AVX2)
    endif()
    check_c_compiler_flag("-mavx512f" HAS_AVX512)
    check_c_compiler_flag("-mxop" HAS_XOP)

    set(NATIVE_TUNING "-march=native -mtune=native")
    check_c_compiler_flag(${NATIVE_TUNING} HAS_NATIVE_TUNING)
    if(NOT HAS_NATIVE_TUNING)
        set(NATIVE_TUNING "")
    endif()

    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ARCH)
    message(STATUS "Detected architecture: ${ARCH}")

    if(HAS_AVX512)
        set(XKCP_IMPL_DIR "AVX512")
    elseif(HAS_AVX2)
        set(XKCP_IMPL_DIR "AVX2")
    elseif(HAS_AVX)
        set(XKCP_IMPL_DIR "AVX")
    elseif(HAS_XOP)
        set(XKCP_IMPL_DIR "XOP")
    elseif(HAS_SSSE3)
        set(XKCP_IMPL_DIR "SSSE3")
    elseif(ARCH MATCHES "x86_64" OR ARCH MATCHES "amd64")
        set(XKCP_IMPL_DIR "generic64")
    elseif(ARCH MATCHES "x86" OR ARCH MATCHES "i[3-6]86")
        set(XKCP_IMPL_DIR "generic32")
    elseif(ARCH MATCHES "armv6[^m]?$")
        set(XKCP_IMPL_DIR "ARMv6")
    elseif(ARCH MATCHES "armv6m")
        set(XKCP_IMPL_DIR "ARMv6M")
    elseif(ARCH MATCHES "armv7[-]?m")
        set(XKCP_IMPL_DIR "ARMv7M")
    elseif(ARCH MATCHES "armv7[-]?a" OR ARCH MATCHES "armv7l" OR ARCH MATCHES "armv8l")
        set(XKCP_IMPL_DIR "ARMv7A")
    elseif(ARCH MATCHES "armv8[-]?a")
        set(XKCP_IMPL_DIR "ARMv8A")
    elseif(ARCH MATCHES "armv8")
        set(XKCP_IMPL_DIR "ARMv8")
    elseif(ARCH MATCHES "avr")
        set(XKCP_IMPL_DIR "AVR8")
    else()
        set(XKCP_IMPL_DIR "compact")
    endif()

    message(STATUS "Using XKCP implementation: ${XKCP_IMPL_DIR}")

    ExternalProject_Add(
        XKCP
        GIT_REPOSITORY https://github.com/XKCP/XKCP.git
        GIT_SHALLOW TRUE
        BUILD_IN_SOURCE ON
        CONFIGURE_COMMAND ""
        TEST_COMMAND ""

        BUILD_COMMAND ${CMAKE_COMMAND} -E env EXTRAFLAGS=${NATIVE_TUNING} make ${XKCP_IMPL_DIR}/libXKCP.a EXTRA_CFLAGS=${NATIVE_TUNING}

        INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/XKCP/lib/${XKCP_IMPL_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/XKCP/include/${XKCP_IMPL_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy bin/${XKCP_IMPL_DIR}/libXKCP.a ${CMAKE_BINARY_DIR}/XKCP/lib/${XKCP_IMPL_DIR}/libXKCP.a
        COMMAND ${CMAKE_COMMAND} -E copy_directory bin/${XKCP_IMPL_DIR}/libXKCP.a.headers/ ${CMAKE_BINARY_DIR}/XKCP/include/${XKCP_IMPL_DIR}/

        BUILD_BYPRODUCTS "${CMAKE_BINARY_DIR}/XKCP/lib/${XKCP_IMPL_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}XKCP${CMAKE_STATIC_LIBRARY_SUFFIX}"
        STAMP_DIR ${CMAKE_BINARY_DIR}/stamps
        UPDATE_DISCONNECTED TRUE
    )

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/XKCP/include/${XKCP_IMPL_DIR}")

    add_library(XKCP_imported STATIC IMPORTED)
    add_dependencies(XKCP_imported XKCP)
    set_target_properties(XKCP_imported PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/XKCP/lib/${XKCP_IMPL_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}XKCP${CMAKE_STATIC_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/XKCP/include/${XKCP_IMPL_DIR}"
    )
    add_library(XKCP::XKCP ALIAS XKCP_imported)
endif()

configure_file(Thrift-Project.h.in Thrift-Project.h)

include(sources.cmake)

add_executable(thrift ${SOURCE_FILES})

target_include_directories(thrift PUBLIC "${PROJECT_BINARY_DIR}" include)
# target_link_libraries(thrift PUBLIC memalloc zot)


target_include_directories(thrift
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include >
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(thrift PUBLIC memalloc::memalloc)
target_link_libraries(thrift PUBLIC zot::zot)
target_link_libraries(thrift PUBLIC ison::ison)
target_link_libraries(thrift PRIVATE sodium::sodium)
target_link_libraries(thrift PRIVATE blake3::blake3)
target_link_libraries(thrift PRIVATE XKCP::XKCP)

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

