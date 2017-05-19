project(${CORE} C)

set(CORE_SOURCES
        src/knd_attr.c
        src/knd_coderef.c
        src/knd_conc.c
        src/knd_dataclass.c
        src/knd_elem.c
        src/knd_facet.c
        src/knd_msg.c
        src/knd_num.c
        src/knd_object.c
        src/knd_objref.c
        src/knd_output.c
        src/knd_policy.c
        src/knd_query.c
        src/knd_refset.c
        src/knd_repo.c
        src/knd_sorttag.c
        src/knd_spec.c
        src/knd_text.c
        src/knd_user.c
        src/knd_utils.c
        src/knd_array.c
        src/knd_dict.c
        src/knd_list.c)

add_library(${CORE}_obj OBJECT ${CORE_SOURCES})

add_library(${CORE}_static STATIC $<TARGET_OBJECTS:${CORE}_obj>)

