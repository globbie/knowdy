
set(SOURCES
        src/main.c
        src/learner-service.c
        $<TARGET_OBJECTS:glb-lib-options>
)

add_executable(${BINARY} ${SOURCES})

target_link_libraries(${BINARY} pthread ${ZEROMQ_LIBRARY} ${CORE}_static kmq)

install(TARGETS ${BINARY} DESTINATION usr/bin)
