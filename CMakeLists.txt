cmake_minimum_required(VERSION 3.20)

project(Thrift-Project VERSION 1.0.1)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# include(FetchContent)
# FetchContent_Declare(
#   gtest
#   URL file:///sdcard/Jay/Projects/xab_laud_ubject/googletest-main.zip
#   # URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
#       DOWNLOAD_EXTRACT_TIMESTAMP
#       true)

include(ExternalProject)

list(APPEND CMAKE_PREFIX_PATH
    "${CMAKE_BINARY_DIR}/memalloc/lib/cmake/memalloc"
    "${CMAKE_BINARY_DIR}/zot/lib/cmake/zot"
)

find_package(memalloc QUIET)
if(NOT memalloc_FOUND)
    ExternalProject_Add(
        MemAlloc
        GIT_REPOSITORY https://github.com/iudah/memalloc.git
        GIT_SHALLOW TRUE
        INSTALL_DIR ${CMAKE_BINARY_DIR}/memalloc
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        STAMP_DIR ${CMAKE_BINARY_DIR}/stamps
    )
endif()

find_package(zot QUIET)
if(NOT zot_FOUND)
    ExternalProject_Add(
        Zot
        GIT_REPOSITORY https://github.com/iudah/zot.git
        GIT_SHALLOW TRUE
        INSTALL_DIR ${CMAKE_BINARY_DIR}/zot
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
        STAMP_DIR ${CMAKE_BINARY_DIR}/stamps
    )
endif()

configure_file(Thrift-Project.h.in Thrift-Project.h)

include(sources.cmake)

add_executable(thrift ${SOURCE_FILES})

target_include_directories(thrift PUBLIC "${PROJECT_BINARY_DIR}" include)
target_link_libraries(thrift PRIVATE memalloc zot)

if(NOT memalloc_FOUND)
    target_link_directories(thrift PRIVATE ${CMAKE_BINARY_DIR}/memalloc/lib)
    add_dependencies(Zot MemAlloc)
endif()

if(NOT zot_FOUND)
    target_include_directories(thrift PRIVATE ${CMAKE_BINARY_DIR}/zot/include)
    target_link_directories(thrift PRIVATE ${CMAKE_BINARY_DIR}/zot/lib)
    add_dependencies(thrift Zot)
endif()

install(TARGETS thrift DESTINATION bin)

# FetchContent_MakeAvailable(gtest)

# enable_testing()

# add_executable(thrift-project-test test/thrift-test.cpp ${SOURCE_FILES})

# target_link_libraries(thrift-project-test GTest::gtest_main)

# include(GoogleTest)
# gtest_discover_tests(thrift-project-test)
