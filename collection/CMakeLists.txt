set(COLLECTION_SERVER_SOURCES
        src/knd_coll_server.c
        src/knd_collection.c)

add_executable(coll_server ${COLLECTION_SERVER_SOURCES})

target_link_libraries(coll_server pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} core_static)

install(TARGETS coll_server DESTINATION bin)
