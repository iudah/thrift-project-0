cmake_minimum_required(VERSION 3.20)

project(Thrift-Project VERSION 0.0.3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  gtest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
      DOWNLOAD_EXTRACT_TIMESTAMP
      true)

configure_file(Thrift-Project.h.in Thrift-Project.h)

add_executable(thrift main.c json_parser.c)

install(TARGETS thrift DESTINATION bin)

FetchContent_MakeAvailable(gtest)

enable_testing()

add_executable(thrift-project-test test/thrift-test.cpp json_parser.c)

target_link_libraries(thrift-project-test GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(thrift-project-test)

