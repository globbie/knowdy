set(SOURCES
        src/main.c
        src/knd-shard-service.c
        src/knd-shard-service.h
        $<TARGET_OBJECTS:glb-lib-options>
)

add_executable(knd-shard ${SOURCES})
target_link_libraries(knd-shard knowdy_static kmq)
