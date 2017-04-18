cmake_minimum_required(VERSION 3.2)
project(knowdy C)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -Wall -Wextra -pedantic")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS} -g3")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -03 -Werrror")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

set(COLLECTION_SERVER
    src/knd_coll_server.c
    src/knd_collection.c
    src/knd_utils.c
)

set(DELIVERY_SERVER
    src/knd_delivery_server.c
    src/knd_delivery.c
    src/knd_output.c
    src/knd_spec.c
    src/knd_utils.c
    src/knd_objref.c
    src/knd_facet.c
    src/knd_sorttag.c
    src/knd_refset.c
    src/knd_coderef.c
    src/knd_monitor.c
    src/oodict.c
    src/ooarray.c
    src/oolist.c
)

set(DATA_READER
    src/knd_data_reader.c
    src/knd_utils.c
    src/knd_output.c
    src/knd_spec.c
    src/knd_dataclass.c
    src/knd_attr.c
    src/knd_query.c
    src/knd_conc.c
    src/knd_coderef.c
    src/knd_objref.c
    src/knd_facet.c
    src/knd_sorttag.c
    src/knd_user.c
    src/knd_repo.c
    src/knd_policy.c
    src/knd_refset.c
    src/knd_text.c
    src/knd_object.c
    src/knd_elem.c
    src/knd_num.c
    src/oodict.c
    src/ooarray.c
    src/oolist.c
)

set(DATA_WRITER
    src/knd_data_writer.c
    src/knd_utils.c
    src/knd_output.c
    src/knd_spec.c
    src/knd_dataclass.c
    src/knd_attr.c
    src/knd_conc.c
    src/knd_coderef.c
    src/knd_objref.c
    src/knd_text.c
    src/knd_facet.c
    src/knd_sorttag.c
    src/knd_query.c
    src/knd_refset.c
    src/knd_user.c
    src/knd_repo.c
    src/knd_policy.c
    src/knd_object.c
    src/knd_elem.c
    src/knd_num.c
    src/oodict.c
    src/ooarray.c
    src/oolist.c
)

set(STORAGE_SERVER
    src/knd_storage_server.c
    src/knd_storage.c
    src/knd_utils.c
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

target_link_libraries(data_writer ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY})
target_link_libraries(data_reader ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY})
target_link_libraries(coll_server ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY})
target_link_libraries(delivery_server ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY})
target_link_libraries(storage_server ${LIBXML2_LIBRARIES} ${ZEROMQ_LIBRARY})
