
set(SOURCES
        src/knd_learner.c)

add_executable(${BINARY} ${SOURCES})

target_link_libraries(${BINARY} pthread ${CURL_LIBRARY} ${ZEROMQ_LIBRARY} ${CORE}_static)

install(TARGETS ${BINARY} DESTINATION usr/bin)
