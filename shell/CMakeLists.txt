pkg_check_modules(READLINE REQUIRED readline)
find_path(READLINE_INCLUDE_DIR NAMES readline.h PATHS ${READLINE_INCLUDE_DIRS})
find_library(READLINE_LIBRARY NAMES readline PATHS ${READLINE_LIBRARY_DIRS})
include_directories(${READLINE_INCLUDE_DIR})

add_executable(knd-shell src/shell.c)
target_link_libraries(knd-shell knowdy_static ${GSL_KNOWDY_LIBRARY} ${GSL_PARSER_LIBRARY} readline)
