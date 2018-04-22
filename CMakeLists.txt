cmake_minimum_required(VERSION 3.2)
project(knowdy C)
find_package(PkgConfig)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_INSTALL_PREFIX /)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -Wall -Wextra -pedantic")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS} -g3 -pg")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -O2 -Wall")

if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "Using 'Debug' build type as default")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

pkg_check_modules(CURL REQUIRED libcurl)
find_path(CURL_INCLUDE_DIR NAMES curl.h PATHS ${CURL_INCLUDE_DIRS})
find_library(CURL_LIBRARY NAMES curl PATHS ${CURL_LIBRARY_DIRS})
include_directories(${CURL_INCLUDE_DIRS})

pkg_check_modules(CHECK REQUIRED check)
find_path(CHECK_INCLUDE_DIR NAMES check.h PATHS ${CHECK_INCLUDE_DIRS})
find_library(CHECK_LIBRARY NAMES check PATHS ${CHECK_LIBRARY_DIRS})
include_directories(${CHECK_INCLUDE_DIR})

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        set(CHECK_LIBRARY ${CHECK_LIBRARY} rt m pthread subunit)
        set(CURL_LIBRARY ${CURL_LIBRARY} curl)
endif()

if(NOT TARGET gsl-parser)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/libs/gsl-parser")
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/libs/gsl-parser/include")
    set(GSL_PARSER_LIBRARY gsl-parser_static)
endif()

if(NOT TARGET glb-lib)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/libs/glb-lib")
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/libs/glb-lib/include")
    set(GLB_LIBRARY glb-lib)
endif()

if(NOT TARGET kmq)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/libs/knode")
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/libs/knode/include")
endif()

set(KNOWDY_PREFIX "knd")
set(CORE      "core")
#set(RETRIEVER "retriever")
set(LEARNER   "learner")

set(KNOWDY_MODULES
        ${CORE}
        ${RETRIEVER}
        ${LEARNER})

include_directories(${CORE}/include)

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


# packaging

install(DIRECTORY etc/knowdy/ DESTINATION etc/knowdy)
install(DIRECTORY etc/systemd/system/ DESTINATION usr/lib/systemd/system)

set(VERSION "0.5.1")

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

