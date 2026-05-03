cmake_minimum_required(VERSION 3.15)

project(
  Zot
  VERSION 2.1.2
  DESCRIPTION "zot memory manager"
  LANGUAGES C)

set(TARGET zot)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(BUILD_EXECUTABLE "Build test executable " ON)
option(ENABLE_SANITIZERS "Enable Address and Undefined Behaviours Sanitizers"
       OFF)
option(USE_ANDROID_LOG "Use Android Log API" OFF)

message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "System Processor: ${CMAKE_SYSTEM_PROCESSOR}")

include(ExternalProject)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/memalloc/lib/cmake/memalloc")

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
               -DENABLE_SANITIZERS=${ENABLE_SANITIZERS}
    BUILD_ALWAYS OFF
    STAMP_DIR ${CMAKE_BINARY_DIR}/stamps
    UPDATE_DISCONNECTED TRUE)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/memalloc/include")

  add_library(memalloc_imported STATIC IMPORTED)
  add_dependencies(memalloc_imported MemAlloc)
  set_target_properties(
    memalloc_imported
    PROPERTIES
      IMPORTED_LOCATION
      "${CMAKE_BINARY_DIR}/memalloc/lib/${CMAKE_STATIC_LIBRARY_PREFIX}memalloc${CMAKE_STATIC_LIBRARY_SUFFIX}"
      INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/memalloc/include")

  add_library(memalloc::memalloc ALIAS memalloc_imported)
endif()

set(HEADER_FILES include/zot.h)
set(SOURCE_FILES src/zot.c)

add_library(${TARGET} STATIC ${SOURCE_FILES})
add_library(zot::${TARGET} ALIAS ${TARGET})

target_include_directories(
  ${TARGET} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                   $<INSTALL_INTERFACE:include>)
target_link_libraries(${TARGET} PUBLIC memalloc::memalloc)
if(${USE_ANDROID_LOG})
  target_compile_definitions(${TARGET} PRIVATE USE_ANDROID_LOG)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL
                                        "RelWithDebInfo")
  if(MSVC)
    target_compile_options(${TARGET} PRIVATE /Zi /Od /RTC1)
    target_link_options(${TARGET} PRIVATE /DEBUG)
  else()
    target_compile_options(${TARGET} PRIVATE -g -O0 -fno-omit-frame-pointer)
  endif()
  if(ENABLE_SANITIZERS AND NOT MSVC)
    message(STATUS "Sanitizers Loading")
    target_compile_options(${TARGET} PRIVATE -fsanitize=address
                                             -fsanitize=undefined)
    target_link_options(${TARGET} PRIVATE -fsanitize=address
                        -fsanitize=undefined)
  endif()
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm"
   OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64"
   AND NOT MSVC)
  include(CheckCCompilerFlag)
  check_c_compiler_flag("-mfpu=neon" HAS_NEON_FLAG)
  if(HAS_NEON_FLAG)
    target_compile_options(${TARGET} PRIVATE -mfpu=neon)
  endif()
endif()

# if(BUILD_EXECUTABLE) add_executable(${TARGET}_exe main.c)
# target_link_libraries(${TARGET}_exe PRIVATE ${TARGET})
# target_include_directories(${TARGET}_exe PRIVATE
# ${CMAKE_CURRENT_SOURCE_DIR}/include) endif()

include(GNUInstallDirs)

install(
  TARGETS ${TARGET}
  EXPORT ZotTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES ${HEADER_FILES} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
  EXPORT ZotTargets
  FILE zot-config.cmake
  NAMESPACE zot::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zot)

export(EXPORT ZotTargets FILE "${CMAKE_BINARY_DIR}/zot-config.cmake")
