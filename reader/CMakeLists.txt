set(DATA_READER_SOURCES
        src/knd_data_reader.c)

add_executable(data_reader ${DATA_READER_SOURCES})

target_link_libraries(data_reader pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} core_static)

install(TARGETS data_reader DESTINATION bin)
