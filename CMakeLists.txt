cmake_minimum_required(VERSION 3.20)

project(Thrift-Project VERSION 0.2.1)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# include(FetchContent)
# FetchContent_Declare(
#   gtest
#   URL file:///sdcard/Jay/Projects/xab_laud_ubject/googletest-main.zip
#   # URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
#       DOWNLOAD_EXTRACT_TIMESTAMP
#       true)

configure_file(Thrift-Project.h.in Thrift-Project.h)

include(sources.cmake)

add_executable(thrift ${SOURCE_FILES})

target_include_directories(thrift PUBLIC "${PROJECT_BINARY_DIR}" include)

install(TARGETS thrift DESTINATION bin)

# FetchContent_MakeAvailable(gtest)

# enable_testing()

# add_executable(thrift-project-test test/thrift-test.cpp ${SOURCE_FILES})

# target_link_libraries(thrift-project-test GTest::gtest_main)

# include(GoogleTest)
# gtest_discover_tests(thrift-project-test)
