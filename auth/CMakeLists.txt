set(SOURCES
        src/knd_auth.c
        src/knd_auth_server.c)

add_executable(${BINARY} ${SOURCES})

target_link_libraries(${BINARY} pthread ${ZEROMQ_LIBRARY} ${MYSQL_LIBRARY} ${CORE}_static ${PARSER}_static)

install(TARGETS ${BINARY} DESTINATION usr/bin)
