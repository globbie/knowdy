
set(SOURCES
        src/knd_learner.c)

add_executable(${BINARY} ${SOURCES})
add_executable(${BINARY}-new src/main.c src/learner-service.c)

target_link_libraries(${BINARY} pthread ${ZEROMQ_LIBRARY} ${CORE}_static)
target_link_libraries(${BINARY}-new pthread ${ZEROMQ_LIBRARY} ${CORE}_static glb-options kmq)

install(TARGETS ${BINARY} DESTINATION usr/bin)
