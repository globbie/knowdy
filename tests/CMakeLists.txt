
# set_perf_test
add_executable(set_perf_test set_perf_test.c)
target_link_libraries(set_perf_test knowdy_static)

set(SOURCES knd-shard-test.c)
add_executable(knd-shard-test ${SOURCES})

target_link_libraries(knd-shard-test ${CHECK_LIBRARY} knowdy_static)

add_test(NAME shard COMMAND knd-shard-test)
