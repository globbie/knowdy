
# set_perf_test
add_executable(set_perf_test performance/set_perf_test.c)
target_link_libraries(set_perf_test knowdy_static)

# knd-shard-test
add_executable(knd-shard-test knd-shard-test.c)
target_link_libraries(knd-shard-test ${CHECK_LIBRARY} knowdy_static)

# check-knowdy
add_custom_target(check-knowdy COMMENT "runs unit tests for knowdy project")
add_custom_command(TARGET check-knowdy POST_BUILD
    DEPENDS knd-shard-test COMMENT "runs unit tests for shard module"
    COMMAND valgrind --leak-check=full ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/knd-shard-test)
