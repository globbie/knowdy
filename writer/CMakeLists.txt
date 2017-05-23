
set(SOURCES
        src/knd_data_writer.c)

add_executable(${BINARY} ${SOURCES})

target_link_libraries(${BINARY} pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} ${CORE}_static)

install(TARGETS ${BINARY} DESTINATION usr/bin)
