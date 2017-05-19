project(${READER} C)

set(DATA_READER_SOURCES
        src/knd_data_reader.c)

add_executable(${READER} ${DATA_READER_SOURCES})

target_link_libraries(${READER} pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} ${CORE}_static)

install(TARGETS ${READER} DESTINATION bin)
