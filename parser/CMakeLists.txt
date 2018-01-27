set(SOURCES
        src/knd_parser.c)

add_library(${PARSER}_obj OBJECT ${SOURCES})
add_library(${PARSER}_static STATIC $<TARGET_OBJECTS:${PARSER}_obj>)

