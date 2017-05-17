set(DELIVERY_SERVER_SOURCES
        src/knd_delivery.c
        src/knd_delivery_server.c
        src/knd_monitor.c)

add_executable(delivery_server ${DELIVERY_SERVER_SOURCES})

target_link_libraries(delivery_server pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} core_static)

install(TARGETS delivery_server DESTINATION bin)
