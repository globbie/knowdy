set(SOURCES
        src/knd_parser.c)

add_library(${PARSER}_obj OBJECT ${SOURCES})
add_library(${PARSER}_static STATIC $<TARGET_OBJECTS:${PARSER}_obj>)

#set(TEST_BINARY ${PARSER}_tests)
#set(TEST_SOURCES
#        tests/run_tests.c)

#add_executable(${TEST_BINARY} ${TEST_SOURCES})

#target_link_libraries(${TEST_BINARY} ${CHECK_LIBRARY} ${PARSER}_static ${CORE}_static)

