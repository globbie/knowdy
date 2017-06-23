
set(SOURCES
        src/knd_data_writer.c)

add_executable(${BINARY} ${SOURCES})

target_link_libraries(${BINARY} pthread ${ZEROMQ_LIBRARY} ${CORE}_static ${PARSER}_static)

install(TARGETS ${BINARY} DESTINATION usr/bin)
