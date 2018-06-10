set(SOURCES
      src/knd_retriever.c)

add_executable(${BINARY} ${SOURCES})
target_link_libraries(${BINARY} pthread ${CURL_LIBRARY} ${ZEROMQ_LIBRARY} ${CORE}_static)
install(TARGETS ${BINARY} DESTINATION usr/bin)
