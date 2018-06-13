
set(SOURCES
        src/main.c
        src/delivery-service.c
        $<TARGET_OBJECTS:glb-lib-options>
)

add_executable(${BINARY} ${SOURCES})

target_link_libraries(${BINARY} pthread ${CURL_LIBRARY} ${CORE}_static kmq)

install(TARGETS ${BINARY} DESTINATION usr/bin)

