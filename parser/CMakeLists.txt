set(SOURCES
        src/knd_parser.c)

add_library(${PARSER}_obj OBJECT ${SOURCES})
add_library(${PARSER}_static STATIC $<TARGET_OBJECTS:${PARSER}_obj>)

# Tests
#set(TEST_BINARY ${PARSER}_tests)
#set(TEST_SOURCES
#        tests/run_tests.c)

#add_executable(${TEST_BINARY} ${TEST_SOURCES})

pkg_check_modules(CHECK REQUIRED check)
find_path(CHECK_INCLUDE_DIR NAMES check.h PATHS ${CHECK_INCLUDE_DIRS})
find_library(CHECK_LIBRARY NAMES check PATHS ${CHECK_LIBRARY_DIRS})
include_directories(${CHECK_INCLUDE_DIR})

#set(CHECK_LIBRARY check pthread rt m subunit)  # pkg-config --libs check
#target_link_libraries(${TEST_BINARY} ${CHECK_LIBRARY} ${PARSER}_static ${CORE}_static)

