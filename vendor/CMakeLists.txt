add_subdirectory(BLAKE3/c)
add_subdirectory(thrift_crypto)

add_subdirectory(memalloc)
if(TARGET memalloc AND NOT TARGET memalloc::memalloc)
  add_library(memalloc::memalloc ALIAS memalloc)
endif()

add_subdirectory(zot)
if(TARGET zot AND NOT TARGET zot::zot)
  add_library(zot::zot ALIAS zot)
endif()

add_subdirectory(ison)
if(TARGET ison AND NOT TARGET ison::ison)
  add_library(ison::ison ALIAS ison)
endif()


include(ExternalProject)
include(CheckCCompilerFlag)

ExternalProject_Add(
        sodium_vendor
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libsodium
        CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR>
        BUILD_COMMAND make
        INSTALL_COMMAND make install
        BUILD_IN_SOURCE ON
        BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/sodium_vendor-prefix/lib/libsodium.a"
    )


    add_library(sodium STATIC IMPORTED GLOBAL)
    add_dependencies(sodium sodium_vendor)
    set_target_properties(sodium PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/sodium_vendor-prefix/lib/libsodium.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/libsodium/src/libsodium/include"
    )


    include(XKCP.cmake)
    ExternalProject_Add(
        XKCP_vendor
        BUILD_IN_SOURCE ON
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/XKCP

        CONFIGURE_COMMAND ""
        TEST_COMMAND ""

BUILD_COMMAND ${CMAKE_COMMAND} -E env EXTRAFLAGS=${NATIVE_TUNING} make ${XKCP_IMPL_DIR}/libXKCP.a EXTRA_CFLAGS=${NATIVE_TUNING}

        INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/XKCP_out/lib
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/XKCP_out/include
    COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bin/${XKCP_IMPL_DIR}/libXKCP.a ${CMAKE_CURRENT_BINARY_DIR}/XKCP_out/lib/libXKCP.a
    COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/bin/${XKCP_IMPL_DIR}/libXKCP.a.headers/ ${CMAKE_CURRENT_BINARY_DIR}/XKCP_out/include/
        BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/XKCP_out/lib/libXKCP.a"
)

add_library(XKCP STATIC IMPORTED GLOBAL)
add_dependencies(XKCP XKCP_vendor)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/XKCP_out/include")

set_target_properties(XKCP PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/XKCP_out/lib/libXKCP.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/XKCP_out/include"
)

