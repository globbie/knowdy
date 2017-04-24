set(STORAGE_SERVER_SOURCES
        src/knd_storage_server.c
        src/knd_storage.c)

add_executable(storage_server ${STORAGE_SERVER_SOURCES})

target_link_libraries(storage_server pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} core_static)
