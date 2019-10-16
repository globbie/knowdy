add_executable(knd-shell src/shell.c)
target_link_libraries(knd-shell knowdy_static ${GSL_KNOWDY_LIBRARY} ${GSL_PARSER_LIBRARY} readline)
