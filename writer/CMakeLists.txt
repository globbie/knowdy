set(DATA_WRITER_SOURCES
        src/knd_data_writer.c)

add_executable(data_writer ${DATA_WRITER_SOURCES})

target_link_libraries(data_writer pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} core_static)

install(TARGETS data_writer DESTINATION bin)
