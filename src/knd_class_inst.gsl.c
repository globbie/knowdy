#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_set.h"
#include "knd_shared_set.h"

#define DEBUG_CLASS_INST_GSL_LEVEL_TMP 0
#define DEBUG_CLASS_INST_GSL_LEVEL_1 0

static int export_class_inst(void *obj, const char *unused_var(elem_id),
                             size_t unused_var(elem_id_size),
                             size_t unused_var(count), void *elem)
{
    struct kndTask *task = obj;
    size_t indent_size = task->ctx->format_indent;
    size_t depth = task->depth;
    // if (count < task->start_from) return knd_OK;
    // if (task->batch_size >= task->batch_max) return knd_RANGE;

    struct kndOutput *out = task->out;
    struct kndClassInstEntry *inst = elem;
    struct kndClassEntry *entry = inst->is_a;
    int err;

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }
    OUT("{", 1);
    OUT(inst->name, inst->name_size);

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth + 1) * indent_size);
        RET_ERR();
    }

    OUT("{class ", strlen("{class "));
    OUT(entry->name, entry->name_size);
    OUT("}", 1);

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }
    OUT("}", 1);

    task->batch_size++;
    return knd_OK;
}

static int export_inverse_rels(struct kndClassInst *self, struct kndTask *task, size_t depth)
{
    struct kndAttrHub *attr_hub;
    struct kndAttr *attr;
    struct kndOutput *out = task->out;
    bool in_list = false;
    size_t curr_depth = 0;
    size_t indent_size = task->ctx->format_indent;
    int err;

    OUT(",", 1);
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }

    OUT("[rel", strlen("[rel"));
    if (indent_size) {
        OUT(" ", 1);
    }
    
    FOREACH (attr_hub, self->attr_hubs) {
        //if (in_list) {
        //    OUT(",", 1);
        //}
        if (!attr_hub->attr) {
            err = knd_attr_hub_resolve(attr_hub, task);
            KND_TASK_ERR("failed to resolve attr hub");
        }
        attr = attr_hub->attr;
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }

        OUT("{", 1);
        OUT(attr->name, attr->name_size);
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 2) * indent_size);
            RET_ERR();
        }
        OUT("{class ", strlen("{class "));
        OUT(attr->parent_class->name, attr->parent_class->name_size);
        OUT("}", 1);
        
        if (attr_hub->topics) {
            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 2) * indent_size);
                RET_ERR();
            }
            OUT("{total ", strlen("{total "));
            OUTF("%zu", attr_hub->topics->num_valid_elems);
            OUT("}", 1);
 
            curr_depth = task->ctx->max_depth;
            task->ctx->max_depth = 0;
            task->depth = depth + 3;
            task->batch_size = 0;
            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 2) * indent_size);
                RET_ERR();
            }
            OUT("[topic ", strlen("[topic "));
            err = knd_set_map(attr_hub->topics, export_class_inst, (void*)task);
            if (err && err != knd_RANGE) return err;

            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 2) * indent_size);
                RET_ERR();
            }
            OUT("]", 1);
            task->ctx->max_depth = curr_depth;
        }

        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        OUT("}", 1);
        in_list = true;
    }
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }
    OUT("]", 1);
    return knd_OK;
}

int knd_class_inst_export_GSL(struct kndClassInst *self, bool is_list_item,
                              knd_state_phase phase, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    size_t indent_size = task->ctx->format_indent;
    size_t curr_depth;
    bool use_locale = true;
    int err;

    if (DEBUG_CLASS_INST_GSL_LEVEL_1)
        knd_log(".. GSL export: %.*s {phase %d}",
                self->name_size, self->name, phase);

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }

    if (!is_list_item) {
        OUT("{", 1);
        if (phase == KND_CREATED) {
            OUT("!", 1);
        }
        OUT("inst ", strlen("inst "));
    }

    OUT(self->name, self->name_size);
    if (task->ctx->use_numid) {
        OUT("{_id ", strlen("{_id "));
        OUT(self->entry->id, self->entry->id_size);
        OUT("}", 1);
    }

    if (task->ctx->use_alias && self->alias_size) {
        OUT("{_as ", strlen("{_as "));
        OUT(self->alias, self->alias_size);
        OUT("}", 1);
    }

    if (self->tr) {
        switch (phase) {
        case KND_CREATED:
            use_locale = false;
            break;
        default:
            break;
        }
        err = knd_text_gloss_export_GSL(self->tr, use_locale, task, depth + 1);
        KND_TASK_ERR("failed to export gloss GSL");
    }
    
    if (self->linear_pos) {
        err = out->write(out, "{_pos ", strlen("{_pos "));           RET_ERR();
        err = out->writef(out, "%zu", self->linear_pos);             RET_ERR();
        err = out->writec(out, '}');                                 RET_ERR();
    }
    if (self->linear_len) {
        err = out->write(out, "{_len ", strlen("{_len "));             RET_ERR();
        err = out->writef(out, "%zu", self->linear_len);                   RET_ERR();
        err = out->writec(out, '}');                                 RET_ERR();
    }
    if (self->class_var->attrs) {
        curr_depth = task->ctx->depth;
        err = knd_attr_vars_export_GSL(self->class_var->attrs,
                                       task, false, depth + 1);  RET_ERR();
        task->ctx->depth = curr_depth;   
    }

    switch (phase) {
    case KND_SELECTED:
        /* display inverse relations */
        if (self->attr_hubs) {
            err = export_inverse_rels(self, task, depth + 1);
            KND_TASK_ERR("failed to export GSL inverse rels");
        }
        break;
    default:
        break;
    }

    if (!is_list_item) {
        err = out->writec(out, '}');
        RET_ERR();
    }
    return knd_OK;
}
