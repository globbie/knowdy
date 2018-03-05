set(SOURCES
        src/knd_concept.c
        src/knd_attr.c
        src/knd_elem.c
        src/knd_mempool.c
        src/knd_num.c
        src/knd_object.c
        src/knd_output.c
        src/knd_proc.c
        src/knd_proc_arg.c
        src/knd_query.c
        src/knd_ref.c
        src/knd_rel.c
        src/knd_rel_arg.c
        src/knd_set.c
        src/knd_state.c
        src/knd_repo.c
        src/knd_task.c
        src/knd_text.c
        src/knd_user.c
        src/knd_utils.c
        src/knd_array.c
        src/knd_dict.c
        src/knd_list.c)

add_library(${CORE}_obj OBJECT ${SOURCES})

add_library(${CORE}_static STATIC $<TARGET_OBJECTS:${CORE}_obj>)
target_link_libraries(${CORE}_static ${CURL_LIBRARY} ${GSL_PARSER_LIBRARY})

