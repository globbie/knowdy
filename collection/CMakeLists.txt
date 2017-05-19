project(${COLLECTION} C)

set(COLLECTION_SERVER_SOURCES
        src/knd_coll_server.c
        src/knd_collection.c)

add_executable(${COLLECTION} ${COLLECTION_SERVER_SOURCES})

target_link_libraries(${COLLECTION} pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} ${CORE}_static)

install(TARGETS ${COLLECTION} DESTINATION bin)
