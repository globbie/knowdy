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

set(COLLECTION_SERVER
        src/collection/knd_coll_server.c
        src/collection/knd_collection.c
        src/core/knd_msg.c
        src/core/knd_utils.c
)

set(DELIVERY_SERVER
        src/delivery/knd_delivery_server.c
        src/delivery/knd_delivery.c
        src/core/knd_output.c
        src/core/knd_spec.c
        src/core/knd_msg.c
        src/core/knd_utils.c
        src/core/knd_objref.c
        src/core/knd_facet.c
        src/core/knd_sorttag.c
        src/core/knd_refset.c
        src/core/knd_coderef.c
        src/delivery/knd_monitor.c
        src/core/oodict.c
        src/core/ooarray.c
        src/core/oolist.c
)

set(DATA_READER
        src/data_reader/knd_data_reader.c
        src/core/knd_msg.c
        src/core/knd_utils.c
        src/core/knd_output.c
        src/core/knd_spec.c
        src/core/knd_dataclass.c
        src/core/knd_attr.c
        src/core/knd_query.c
        src/core/knd_conc.c
        src/core/knd_coderef.c
        src/core/knd_objref.c
        src/core/knd_facet.c
        src/core/knd_sorttag.c
        src/core/knd_user.c
        src/core/knd_repo.c
        src/core/knd_policy.c
        src/core/knd_refset.c
        src/core/knd_text.c
        src/core/knd_object.c
        src/core/knd_elem.c
        src/core/knd_num.c
        src/core/oodict.c
        src/core/ooarray.c
        src/core/oolist.c
)

set(DATA_WRITER
        src/data_writer/knd_data_writer.c
        src/core/knd_msg.c
        src/core/knd_utils.c
        src/core/knd_output.c
        src/core/knd_spec.c
        src/core/knd_dataclass.c
        src/core/knd_attr.c
        src/core/knd_conc.c
        src/core/knd_coderef.c
        src/core/knd_objref.c
        src/core/knd_text.c
        src/core/knd_facet.c
        src/core/knd_sorttag.c
        src/core/knd_query.c
        src/core/knd_refset.c
        src/core/knd_user.c
        src/core/knd_repo.c
        src/core/knd_policy.c
        src/core/knd_object.c
        src/core/knd_elem.c
        src/core/knd_num.c
        src/core/oodict.c
        src/core/ooarray.c
        src/core/oolist.c
)

set(STORAGE_SERVER
        src/storage/knd_storage_server.c
        src/storage/knd_storage.c
        src/core/knd_msg.c
        src/core/knd_utils.c
)

#set(TEST_CLIENT
#    src/knd_test_client.c
#)

find_package(PkgConfig)

find_package(LibXml2 REQUIRED)
include_directories(${LIBXML2_INCLUDE_DIR})

pkg_check_modules(ZEROMQ REQUIRED libzmq)
find_path(ZEROMQ_INCLUDE_DIR NAMES zmq.h PATHS ${ZEROMQ_INCLUDE_DIRS})
find_library(ZEROMQ_LIBRARY NAMES zmq PATHS ${ZEROMQ_LIBRARY_DIRS})
include_directories(${ZEROMQ_INCLUDE_DIR})

add_executable(data_writer ${DATA_WRITER})
add_executable(data_reader ${DATA_READER})
add_executable(coll_server ${COLLECTION_SERVER})
add_executable(delivery_server ${DELIVERY_SERVER})
add_executable(storage_server ${STORAGE_SERVER})
#add_executable(test_client ${TEST_CLIENT})

target_link_libraries(data_writer pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY})
target_link_libraries(data_reader pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY})
target_link_libraries(coll_server pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY})
target_link_libraries(delivery_server pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY})
target_link_libraries(storage_server pthread ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY})

#configure_file("${CMAKE_CURRENT_SOURCE_DIR}/knd.spec.in" "${CMAKE_CURRENT_BINARY_DIR}/knd.spec" @ONLY IMMEDIATE)
set(CPACK_RPM_USER_BINARY_SPECFILE "${CMAKE_CURRENT_BINARY_DIR}/knd.spec")
include(CPack)
