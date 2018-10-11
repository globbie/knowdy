
# set_perf_test
add_executable(set_perf_test set_perf_test.c)
target_link_libraries(set_perf_test knowdy_static)


set(SOURCES knd-shard-test.c)
add_executable(knd-shard-test ${SOURCES})

target_link_libraries(knd-shard-test ${CHECK_LIBRARY} knowdy_static)

add_custom_target(knd-shard-test-run ALL
    DEPENDS knd-shard-test
    COMMENT "runs unit tests for shard module")
add_custom_command(TARGET knd-shard-test-run POST_BUILD
    COMMAND valgrind --leak-check=full ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/knd-shard-test)
