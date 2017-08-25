cmake_minimum_required(VERSION 3.2)
project(knowdy C)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_INSTALL_PREFIX /)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -Wall -Wextra -pedantic")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS} -g3")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -03 -Werror")

add_definitions(-D_POSIX_SOURCE=200809L)
add_definitions(-D_GNU_SOURCE)
add_definitions(-D_XOPEN_SOURCE=700)
add_definitions(-D_BSD_SOURCE)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

find_package(PkgConfig)

pkg_check_modules(ZEROMQ REQUIRED libzmq)
find_path(ZEROMQ_INCLUDE_DIR NAMES zmq.h PATHS ${ZEROMQ_INCLUDE_DIRS})
find_library(ZEROMQ_LIBRARY NAMES zmq PATHS ${ZEROMQ_LIBRARY_DIRS})
include_directories(${ZEROMQ_INCLUDE_DIR})

set(KNOWDY_PREFIX "knd")
set(CORE "core")
set(PARSER "parser")
set(COLLECTION "collection")
set(RETRIEVER "retriever")
set(LEARNER "learner")
set(DELIVERY "delivery")
set(STORAGE "storage")

set(KNOWDY_MODULES
        ${CORE}
        ${PARSER}
        ${COLLECTION}
        ${RETRIEVER}
        ${LEARNER}
        ${DELIVERY}
        ${STORAGE})

include_directories(${CORE}/include)
include_directories(${PARSER}/include)

foreach(MODULE ${KNOWDY_MODULES})
    include_directories(${MODULE}/src)
endforeach()

foreach(MODULE ${KNOWDY_MODULES})
    set(${MODULE}_BINARY ${KNOWDY_PREFIX}-${MODULE})
endforeach()

foreach(MODULE ${KNOWDY_MODULES})
    set(BINARY ${${MODULE}_BINARY})
    add_subdirectory(${MODULE})
endforeach()

install(DIRECTORY etc/knowdy/ DESTINATION etc/knowdy)
install(DIRECTORY etc/systemd/system/ DESTINATION usr/lib/systemd/system)

set(VERSION "0.2.2")

set(CPACK_PACKAGE_VERSION ${VERSION})
set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_RELEASE 1)
set(CPACK_PACKAGE_CONTACT "info@globbie.net")
set(CPACK_PACKAGE_VENDOR "globbie")
set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}.${CMAKE_SYSTEM_PROCESSOR}")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/knd.spec.in" "${CMAKE_CURRENT_BINARY_DIR}/knd.spec" @ONLY IMMEDIATE)
set(CPACK_RPM_USER_BINARY_SPECFILE "${CMAKE_CURRENT_BINARY_DIR}/knd.spec")

include(CPack)

