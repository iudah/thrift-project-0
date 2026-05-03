cmake_minimum_required(VERSION 3.19)

project(Ison VERSION 1.0.0 DESCRIPTION "ison: a json parser" LANGUAGES C)

set(TARGET ison)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(BUILD_EXECUTABLE "Build test executable " ON)
option(ENABLE_SANITIZERS "Enable Address and Undefined Behaviours Sanitizers" OFF)
option(BUILD_TESTS "Build tests" OFF)

message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "System Processor: ${CMAKE_SYSTEM_PROCESSOR}")

include(ExternalProject)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/memalloc/lib/cmake/memalloc"
    "${CMAKE_BINARY_DIR}/zot/lib/cmake/zot")

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
    add_dependencies(zot_imported zot MemAlloc)
    set_target_properties(zot_imported PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/zot/lib/${CMAKE_STATIC_LIBRARY_PREFIX}zot${CMAKE_STATIC_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/zot/include"
    )
    add_library(zot::zot ALIAS zot_imported)
endif()

set(SIPHASH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/siphash)

set(HEADER_FILES ison.h ison_data.h ds.h ${SIPHASH_DIR}/siphash.h)
set(SOURCE_FILES ison.c hash-map.c array.c ${SIPHASH_DIR}/siphash.c)

add_library(${TARGET} STATIC ${SOURCE_FILES} ${SIPHASH_SRC})
add_library(ison::${TARGET} ALIAS ${TARGET})

target_include_directories(${TARGET}
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${SIPHASH_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(${TARGET} PUBLIC memalloc::memalloc)
target_link_libraries(${TARGET} PUBLIC zot::zot)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    if(MSVC)
        target_compile_options(${TARGET} PRIVATE /Zi /Od /RTC1)
        target_link_options(${TARGET} PRIVATE /DEBUG)
    else()
        target_compile_options(${TARGET} PRIVATE -g -O0 -fno-omit-frame-pointer)
    endif()
    if(ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options(${TARGET} PRIVATE -fsanitize=address -fsanitize=undefined)
        target_link_options(${TARGET} PRIVATE -fsanitize=address -fsanitize=undefined)
    endif()
endif()

if((CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64") AND NOT MSVC)
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-mfpu=neon" HAS_NEON_FLAG)
    if(HAS_NEON_FLAG)
        target_compile_options(${TARGET} PRIVATE -mfpu=neon)
    endif()
endif()

if(BUILD_EXECUTABLE)
    add_executable(ison-exe main.c)
    target_link_libraries(ison-exe PRIVATE ${TARGET})
endif()

# if(BUILD_TESTS)
#     include(include_google_test.cmake)
#     add_executable(ison_test tests/isontest.cpp tests/isonsubclass.cpp)
#     target_link_libraries(ison_test PRIVATE ${TARGET} gtest gtest_main)
#     enable_testing()

#     include(GoogleTest)
#     gtest_discover_tests(ison_test)
# endif()

include(GNUInstallDirs)

install(TARGETS ${TARGET}
    EXPORT IsonTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES ${HEADER_FILES} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT IsonTargets
    FILE ison-config.cmake
    NAMESPACE ison::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ison)

export(EXPORT IsonTargets
    FILE "${CMAKE_BINARY_DIR}/ison-config.cmake")
