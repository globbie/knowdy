cmake_minimum_required(VERSION 3.2)
project(knowdy C)
find_package(PkgConfig)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_INSTALL_PREFIX /)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -Wall -Wextra -pedantic -D_GNU_SOURCE")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g3 -Wno-gnu-zero-variadic-macro-arguments")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")

if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "Using 'Debug' build type as default")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake_modules)

#parsing the semantic version
include(GetGitRevisionDescription)
git_describe(VERSION --tags --dirty=-dirty)
string(REGEX REPLACE "^v([0-9]+)\\..*" "\\1" VERSION_MAJOR "${VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.([0-9]+).*" "\\1" VERSION_MINOR "${VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" VERSION_PATCH "${VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.[0-9]+(.*)" "\\1" VERSION_SHA1 "${VERSION}")
set(VERSION_SHORT "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/knd_version.h.in
                ${CMAKE_CURRENT_SOURCE_DIR}/include/knd_version.h)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  include(CodeCoverage)
  #APPEND_COVERAGE_COMPILER_FLAGS()
  #SETUP_TARGET_FOR_COVERAGE_LCOV(NAME ${PROJECT_NAME}_coverage
  #                               EXECUTABLE knd-shard-test)
endif()

pkg_check_modules(CHECK REQUIRED check)
find_path(CHECK_INCLUDE_DIR NAMES check.h PATHS ${CHECK_INCLUDE_DIRS})
find_library(CHECK_LIBRARY NAMES check PATHS ${CHECK_LIBRARY_DIRS})
include_directories(${CHECK_INCLUDE_DIR})

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CHECK_LIBRARY ${CHECK_LIBRARY} rt m pthread subunit)
endif()

include(ExternalProject)

ExternalProject_Add(gsl-parser-external
    PREFIX ${CMAKE_BINARY_DIR}/libs/gsl-parser
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/gsl-parser
    BINARY_DIR ${CMAKE_BINARY_DIR}/libs/gsl-parser
    CMAKE_ARGS
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    BUILD_COMMAND make gsl-parser_static
    BUILD_ALWAYS TRUE
    INSTALL_COMMAND ""
)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/libs/gsl-parser/include")
set(GSL_PARSER_LIBRARY "${CMAKE_BINARY_DIR}/libs/gsl-parser/lib/libgsl-parser_static.a")

include_directories("include")

set(HEADERS include/knd_attr.h
        include/knd_class.h include/knd_class_inst.h include/knd_config.h
        include/knd_cache.h include/knd_dict.h include/knd_shared_dict.h
        include/knd_err.h
        include/knd_facet.h include/knd_http_codes.h
        include/knd_logic.h include/knd_ignore.h include/knd_mempool.h include/knd_num.h
        include/knd_proc.h include/knd_proc_arg.h include/knd_proc_call.h
        include/knd_queue.h include/knd_query.h
        include/knd_repo.h
        include/knd_set.h include/knd_shared_set.h include/knd_set_idx.h include/knd_shard.h
        include/knd_state.h include/knd_commit.h
        include/knd_task.h include/knd_text.h
        include/knd_user.h include/knd_utils.h include/knd_version.h)
set(SOURCES src/knd_attr.c src/knd_attr.import.c
        src/knd_attr.read.c src/knd_attr.resolve.c src/knd_attr.select.c
        src/knd_attr.json.c src/knd_attr.gsl.c src/knd_attr.gsp.c
        src/knd_attr_var.import.c src/knd_attr_var.index.c src/knd_attr_var.resolve.c src/knd_attr_var.read.c
        src/knd_attr_var.gsl.c src/knd_attr_var.json.c src/knd_attr_var.gsp.c
        src/knd_class.c src/knd_class.gsl.c src/knd_class.gsp.c src/knd_class.read.c
        src/knd_class.import.c src/knd_class.json.c src/knd_class.resolve.c src/knd_class.index.c
        src/knd_class.select.c src/knd_class.states.c
        src/knd_class_inst.c src/knd_class_inst.gsl.c src/knd_class_inst.gsp.c
        src/knd_class_inst.import.c src/knd_class_inst.read.c
        src/knd_class_inst.json.c src/knd_class_inst.select.c
        src/knd_class_inst.resolve.c src/knd_class_inst.index.c
        src/knd_cache.c src/knd_logic.c src/knd_logic.import.c
        src/knd_dict.c src/knd_shared_dict.c src/knd_ignore.c
        src/knd_mempool.c src/knd_num.c src/knd_output.c
        src/knd_proc.c src/knd_proc.import.c  src/knd_proc.resolve.c src/knd_proc.select.c 
        src/knd_proc.gsl.c src/knd_proc.json.c
        src/knd_proc_inst.c src/knd_proc_inst.import.c
        src/knd_proc.svg.c src/knd_proc_arg.c
        src/knd_proc_arg.gsl.c src/knd_proc_call.c
        src/knd_repo.c src/knd_repo.gsl.c src/knd_repo.gsp.c
        src/knd_repo.commit.c src/knd_repo.open.c
        src/knd_set.c src/knd_shared_set.c src/knd_shared_set.gsp.c src/knd_set_idx.c
        src/knd_shard.c
        src/knd_state.c src/knd_commit.c
        src/knd_task.c src/knd_task.select.c
        src/knd_text.c src/knd_text.import.c src/knd_text.resolve.c src/knd_text.index.c
        src/knd_text.read.c src/knd_text.search.c src/knd_text.select.c
        src/knd_text.gsl.c src/knd_text.gsp.c src/knd_text.json.c
        src/knd_user.c src/knd_utils.c)

add_library(${PROJECT_NAME}_obj OBJECT ${HEADERS} ${SOURCES})
add_library(${PROJECT_NAME}_static STATIC $<TARGET_OBJECTS:${PROJECT_NAME}_obj>)
target_link_libraries(${PROJECT_NAME}_static ${GSL_PARSER_LIBRARY})

enable_testing()

#add_subdirectory(tests)
add_subdirectory(shell)

install(DIRECTORY etc/knowdy/ DESTINATION etc/knowdy)
install(DIRECTORY etc/systemd/system/ DESTINATION usr/lib/systemd/system)
