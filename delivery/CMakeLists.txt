project(${DELIVERY} C)

set(DELIVERY_SOURCES
        src/knd_delivery.c
        src/knd_delivery_server.c
        src/knd_monitor.c)

add_executable(${PROJECT_NAME} ${DELIVERY_SOURCES})

target_link_libraries(${PROJECT_NAME} pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} ${CORE}_static)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
