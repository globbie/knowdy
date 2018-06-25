set(SOURCES
        main.c
        lint.c
        $<TARGET_OBJECTS:glb-lib-options>)

add_executable(${BINARY} ${SOURCES})
target_link_libraries(${BINARY} ${CORE}_static kmq)
install(TARGETS ${BINARY} DESTINATION usr/bin)