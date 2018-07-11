set(HEADERS include/knd_array.h include/knd_attr.h
        include/knd_class.h include/knd_class_inst.h include/knd_config.h
        include/knd_dict.h
        include/knd_elem.h include/knd_err.h
        include/knd_facet.h include/knd_http_codes.h include/knd_list.h
        include/knd_mempool.h include/knd_num.h 
        include/knd_proc.h include/knd_proc_arg.h
        include/knd_query.h
        include/knd_ref.h include/knd_rel.h include/knd_rel_arg.h include/knd_repo.h
        include/knd_set.h include/knd_shard.h include/knd_state.h
        include/knd_task.h include/knd_text.h
        include/knd_user.h include/knd_utils.h
        include/ooconfig.h)
set(SOURCES src/knd_array.c src/knd_attr.c
        src/knd_class.c src/knd_class_inst.c src/knd_dict.c src/knd_elem.c src/knd_list.c
        src/knd_mempool.c src/knd_num.c
        src/knd_proc.c src/knd_proc_arg.c
        src/knd_ref.c src/knd_rel.c src/knd_rel_arg.c src/knd_repo.c
        src/knd_set.c src/knd_shard.c src/knd_state.c
        src/knd_task.c src/knd_text.c
        src/knd_user.c src/knd_utils.c)

add_library(${CORE}_obj OBJECT ${HEADERS} ${SOURCES})
add_library(${CORE}_static STATIC $<TARGET_OBJECTS:${CORE}_obj>)
target_link_libraries(${CORE}_static ${GSL_PARSER_LIBRARY})

