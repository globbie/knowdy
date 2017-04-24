cmake_minimum_required(VERSION 3.2)
project(knowdy C)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -Wall -Wextra -pedantic")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS} -g3")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -03 -Werrror")

add_definitions(-D_POSIX_SOURCE=200809L)
add_definitions(-D_GNU_SOURCE)
add_definitions(-D_XOPEN_SOURCE=700)
add_definitions(-D_BSD_SOURCE)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

find_package(PkgConfig)

find_package(LibXml2 REQUIRED)
include_directories(${LIBXML2_INCLUDE_DIR})

pkg_check_modules(ZEROMQ REQUIRED libzmq)
find_path(ZEROMQ_INCLUDE_DIR NAMES zmq.h PATHS ${ZEROMQ_INCLUDE_DIRS})
find_library(ZEROMQ_LIBRARY NAMES zmq PATHS ${ZEROMQ_LIBRARY_DIRS})
include_directories(${ZEROMQ_INCLUDE_DIR})

include_directories(core/include)

include_directories(collection)
include_directories(data_reader)
include_directories(data_writer)
include_directories(delivery)
include_directories(storage)

add_subdirectory(core)

set(COLLECTION_SERVER_SOURCES
        collection/knd_coll_server.c
        collection/knd_collection.c)

set(DELIVERY_SERVER_SOURCES
        delivery/knd_delivery.c
        delivery/knd_delivery_server.c
        delivery/knd_monitor.c)

set(STORAGE_SERVER_SOURCES
        storage/knd_storage_server.c
        storage/knd_storage.c)

set(DATA_READER_SOURCES
        data_reader/knd_data_reader.c)

set(DATA_WRITER_SOURCES
        data_writer/knd_data_writer.c)


add_executable(data_writer ${DATA_WRITER_SOURCES})
add_executable(data_reader ${DATA_READER_SOURCES})
add_executable(coll_server ${COLLECTION_SERVER_SOURCES})
add_executable(delivery_server ${DELIVERY_SERVER_SOURCES})
add_executable(storage_server ${STORAGE_SERVER_SOURCES})

target_link_libraries(data_writer pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} core_static)
target_link_libraries(data_reader pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} core_static)
target_link_libraries(coll_server pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} core_static)
target_link_libraries(delivery_server pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} core_static)
target_link_libraries(storage_server pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY} core_static)

#configure_file("${CMAKE_CURRENT_SOURCE_DIR}/knd.spec.in" "${CMAKE_CURRENT_BINARY_DIR}/knd.spec" @ONLY IMMEDIATE)
set(CPACK_RPM_USER_BINARY_SPECFILE "${CMAKE_CURRENT_BINARY_DIR}/knd.spec")
include(CPack)
