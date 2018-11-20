set(SOURCES
        src/main.c
        src/knd-shard-service.c
        src/knd-shard-service.h
)

add_executable(knd-shard ${SOURCES})
target_link_libraries(knd-shard knowdy_static ${GLB_LIBRARY} ${KNODE_LIBRARY})
