set(DATA_WRITER_SOURCES
        src/knd_data_writer.c)

add_executable(${WRITER} ${DATA_WRITER_SOURCES})

target_link_libraries(${WRITER} pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} ${CORE}_static)

install(TARGETS ${WRITER} DESTINATION bin)
