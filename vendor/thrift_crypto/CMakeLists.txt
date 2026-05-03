set(SOURCES base58.c blake3.c)
set(HEADERS libbase58.h)


add_library(thrift_crypto STATIC ${SOURCES})
target_sources(thrift_crypto PUBLIC ${SOURCES} ${HEADERS})
target_include_directories(thrift_crypto PUBLIC ../BLAKE3/c)
