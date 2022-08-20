#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_shared_set.h"

#define DEBUG_CLASS_INST_GSL_LEVEL_TMP 0
#define DEBUG_CLASS_INST_GSL_LEVEL_1 0

int knd_class_inst_export_GSL(struct kndClassInst *self, bool is_list_item,
                              knd_state_phase phase, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    size_t indent_size = task->ctx->format_indent;
    size_t curr_depth;
    bool use_locale = true;
    int err;

    if (DEBUG_CLASS_INST_GSL_LEVEL_1)
        knd_log(".. GSL export: %.*s {gloss %p} {phase %d}",
                self->name_size, self->name, self->tr, phase);

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
        err = out->write(out, "{_pos ", strlen("{_pos "));             RET_ERR();
        err = out->writef(out, "%zu", self->linear_pos);                   RET_ERR();
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

    if (!is_list_item) {
        err = out->writec(out, '}');
        RET_ERR();
    }

    return knd_OK;
}
