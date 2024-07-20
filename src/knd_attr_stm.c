#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_proc_call.h"
#include "knd_task.h"
#include "knd_class.h"
#include "knd_text.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_STM_LEVEL_1 0
#define DEBUG_ATTR_STM_LEVEL_2 0
#define DEBUG_ATTR_STM_LEVEL_3 0
#define DEBUG_ATTR_STM_LEVEL_4 0
#define DEBUG_ATTR_STM_LEVEL_5 0
#define DEBUG_ATTR_STM_LEVEL_TMP 1

void knd_attr_stm_str(struct kndAttrStm *var, size_t depth)
{
    struct kndAttr *attr = var->attr;
    if (var->is_list_item)
        attr = var->parent->attr;

    struct kndAttrStm *item;
    const char *type_name = "";

    assert (attr != NULL);

    type_name = knd_attr_names[attr->type];
    if (var->is_list_item) {
        switch (attr->type) {
        case KND_ATTR_INNER:
            knd_log("%*s* {inner-class %.*s}", depth * KND_OFFSET_SIZE, "",
                    attr->class_entry->name_size, attr->class_entry->name);
            break;
        case KND_ATTR_REF:
            knd_log("%*s* {class-ref %.*s}", depth * KND_OFFSET_SIZE, "",
                    attr->class_entry->name_size, attr->class_entry->name);
            break;
        default:
            knd_log("%*s* {%s %.*s}", depth * KND_OFFSET_SIZE, "",
                    type_name, var->name_size, var->name);
            break;
        }
        if (var->implied_attr) {
            type_name = knd_attr_names[var->implied_attr->type];
            knd_log("%*s_implied: \"%.*s\" (%s) => %.*s", (depth + 1) * KND_OFFSET_SIZE, "",
                    var->implied_attr->name_size, var->implied_attr->name,
                    type_name, var->val_size, var->val);
        }
        return;
    }

    if (attr->is_a_set) {
        knd_log("%*s%.*s (%s)  [", depth * KND_OFFSET_SIZE, "",
                var->name_size, var->name, type_name);

        FOREACH (item, var->list)
            knd_attr_stm_str(item, depth + 1);

        knd_log("%*s]", depth * KND_OFFSET_SIZE, "");
        return;
    }

    switch (attr->type) {
        case KND_ATTR_INNER:
            knd_log("%*s%.*s (inner \"%.*s\")", depth * KND_OFFSET_SIZE, "",
                    var->name_size, var->name,
                    attr->class_entry->name_size, attr->class_entry->name);
            FOREACH (item, var->children) {
                knd_log("var: %.*s", item->name_size, item->name);
                knd_attr_stm_str(item, depth + 1);
            }
            break;
        case KND_ATTR_REF:
            knd_log("%*s%.*s (\"%.*s\" class ref) => %.*s", depth * KND_OFFSET_SIZE, "",
                    var->name_size, var->name,
                    attr->class_entry->name_size, attr->class_entry->name,
                    var->class_entry->name_size, var->class_entry->name);
            return;
        case KND_ATTR_REL:
            knd_log("%*s%.*s (\"%.*s\" rel)", depth * KND_OFFSET_SIZE, "",
                    var->name_size, var->name,
                    attr->class_entry->name_size, attr->class_entry->name);
            if (var->class_inst_entry) {
                knd_log("%*s=> \"%.*s\"", depth * KND_OFFSET_SIZE, "",
                    var->class_inst_entry->name_size, var->class_inst_entry->name);
            }
            return;
        case KND_ATTR_TEXT:
            knd_log("%*s%.*s:", depth * KND_OFFSET_SIZE, "", var->name_size, var->name);
            knd_text_str(var->text, depth + 1);
            return;
        default:
            knd_log("%*s%.*s (%s) => %.*s", depth * KND_OFFSET_SIZE, "",
                   var->name_size, var->name,  type_name, var->val_size, var->val);
            break;
    }
}
