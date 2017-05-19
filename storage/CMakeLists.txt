set(STORAGE_SERVER_SOURCES
        src/knd_storage_server.c
        src/knd_storage.c)

add_executable(${STORAGE} ${STORAGE_SERVER_SOURCES})

target_link_libraries(${STORAGE} pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} ${CORE}_static)

install(TARGETS ${STORAGE} DESTINATION bin)
