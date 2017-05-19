set(SOURCES
        src/knd_delivery.c
        src/knd_delivery_server.c
        src/knd_monitor.c)

add_executable(${BINARY} ${SOURCES})

target_link_libraries(${BINARY} pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} ${CORE}_static)

install(TARGETS ${BINARY} DESTINATION bin)
