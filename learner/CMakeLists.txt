set(SOURCES
        src/main.c
        src/learner-service.c
        src/learner-service.h
        $<TARGET_OBJECTS:glb-lib-options>
)

add_executable(knd-learner ${SOURCES})
target_link_libraries(knd-learner knowdy_static kmq)
