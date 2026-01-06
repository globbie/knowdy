cmake_minimum_required(VERSION 3.2)
project(knowdy C)

find_package(PkgConfig)

option(KND_PYTHON_SUPPORT "Build Python C extension module" OFF)

if(KND_PYTHON_SUPPORT)
    find_package(PythonInterp 3.5 REQUIRED)
    find_package(PythonLibs 3.5 REQUIRED)
endif()

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

set(HEADERS include/knd_attr.h include/knd_attr_stm.h include/knd_rel.h
        include/knd_class.h include/knd_class_inst.h include/knd_config.h
        include/knd_dict.h include/knd_shared_dict.h
        include/knd_err.h include/knd_facet.h  include/knd_http_codes.h
        include/knd_logic.h include/knd_ignore.h
        include/knd_memblock.h include/knd_mempool.h include/knd_quant.h
        include/knd_proc.h include/knd_proc_arg.h include/knd_proc_call.h
        include/knd_query.h
        include/knd_repo.h include/knd_storage.h
        include/knd_set.h include/knd_shared_set.h include/knd_set_idx.h include/knd_steward.h
        include/knd_state.h include/knd_commit.h
        include/knd_task.h include/knd_text.h
        include/knd_user.h include/knd_utils.h include/knd_version.h)

set(SOURCES src/attr/knd_attr.c src/attr/knd_attr.import.c src/attr/knd_attr.resolve.c
        src/attr/knd_attr.read.c src/attr/knd_attr.decode.c src/attr/knd_attr.select.c
        src/attr/knd_attr.json.c src/attr/knd_attr.gsl.c src/attr/knd_attr.gsp.c
        src/attr/knd_attr_stm.c src/attr/knd_attr_stm.import.c src/attr/knd_attr_stm.index.c
        src/attr/knd_attr_stm.resolve.c src/attr/knd_attr_stm.select.c
        src/attr/knd_attr_stm.read.c src/attr/knd_attr_stm.decode.c
        src/attr/knd_attr_stm.gsl.c src/attr/knd_attr_stm.json.c src/attr/knd_attr_stm.gsp.c
        src/class/knd_class.c src/class/knd_class.gsl.c
        src/class/knd_class.encode.c src/class/knd_class.decode.c src/class/knd_class.read.c
        src/class/knd_class.import.c src/class/knd_class.json.c
        src/class/knd_class.resolve.c src/class/knd_class.index.c
        src/class/knd_class.select.c src/class/knd_class.states.c
        src/class/knd_class_inst.c src/class/knd_class_inst.gsl.c src/class/knd_class_inst.gsp.c
        src/class/knd_class_inst.import.c src/class/knd_class_inst.read.c
        src/class/knd_class_inst.json.c src/class/knd_class_inst.select.c
        src/class/knd_class_inst.resolve.c src/class/knd_class_inst.index.c
        src/dict/knd_dict.c src/dict/knd_dict.gsp.c src/dict/knd_dict.read.c
        src/dict/knd_shared_dict.c
        src/facet/knd_facet.c src/facet/knd_facet.index.c src/facet/knd_facet.gsp.c
        src/facet/knd_facet.read.c src/facet/knd_facet.select.c
        src/utils/knd_ignore.c
        src/text/knd_logic.c src/text/knd_logic.import.c
        src/mem/knd_memblock.c src/mem/knd_mempool.c src/mem/knd_cache.c src/mem/knd_output.c
        src/proc/knd_proc.c src/proc/knd_proc.import.c
        src/proc/knd_proc.resolve.c src/proc/knd_proc.select.c 
        src/proc/knd_proc.gsl.c src/proc/knd_proc.json.c
        src/proc/knd_proc_inst.c src/proc/knd_proc_inst.import.c
        src/proc/knd_proc_inst.gsl.c src/proc/knd_proc_inst.json.c
        src/proc/knd_proc.svg.c src/proc/knd_proc_arg.c
        src/proc/knd_proc_arg.gsl.c src/proc/knd_proc_arg.json.c src/proc/knd_proc_call.c
        src/quant/knd_quant.c src/quant/knd_quant.index.c src/quant/knd_quant.select.c
        src/query/knd_query.c src/query/knd_query.run.c src/query/knd_query.gsl.c
        src/rel/knd_rel.c src/rel/knd_rel.import.c src/rel/knd_rel.resolve.c
        src/rel/knd_rel_pred.resolve.c
        src/repo/knd_repo.c src/repo/knd_repo.gsl.c src/repo/knd_repo.gsp.c src/repo/knd_repo.select.c
        src/repo/knd_repo.commit.c src/repo/knd_repo.read.c src/repo/knd_repo.cache.c
        src/set/knd_set.c src/set/knd_set.gsp.c src/set/knd_set.read.c
        src/set/knd_shared_set.c
        src/set/knd_shared_set.read.c src/set/knd_set_idx.c
        src/knd_steward.c
        src/state/knd_state.c src/storage/knd_storage.c src/state/knd_commit.c
        src/task/knd_task.c src/task/knd_task.cache.c src/task/knd_task.select.c
        src/text/knd_text.c src/text/knd_text.import.c src/text/knd_text.resolve.c
        src/text/knd_text.read.c src/text/knd_text.decode.c
        src/text/knd_text.search.c src/text/knd_text.select.c
        src/text/knd_text.gsl.c src/text/knd_text.gsp.c src/text/knd_text.json.c
        src/user/knd_user.c src/utils/knd_utils.c)

add_library(${PROJECT_NAME}_obj OBJECT ${HEADERS} ${SOURCES})
add_library(${PROJECT_NAME}_static STATIC $<TARGET_OBJECTS:${PROJECT_NAME}_obj>)
target_link_libraries(${PROJECT_NAME}_static ${GSL_PARSER_LIBRARY})

enable_testing()

#add_subdirectory(tests)
add_subdirectory(shell)

install(DIRECTORY etc/knowdy/ DESTINATION etc/knowdy)
install(DIRECTORY etc/systemd/system/ DESTINATION usr/lib/systemd/system)
