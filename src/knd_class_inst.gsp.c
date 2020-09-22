#include "knd_class_inst.h"
#include "knd_task.h"
#include "knd_utils.h"
#include "knd_attr.h"

#define DEBUG_CLASS_INST_GSP_LEVEL_1 0
#define DEBUG_CLASS_INST_GSP_LEVEL_2 0
#define DEBUG_CLASS_INST_GSP_LEVEL_TMP 1

int knd_class_inst_marshall(void *obj, size_t *output_size, struct kndTask *task)
{
    struct kndClassInstEntry *entry = obj;
    struct kndOutput *out = task->out;
    size_t orig_size = out->buf_size;
    int err;
    assert(entry->inst != NULL);

    err = knd_class_inst_export_GSP(entry->inst, task);
    KND_TASK_ERR("failed to export class inst GSP");

    if (DEBUG_CLASS_INST_GSP_LEVEL_2)
        knd_log(">> GSP of class inst \"%.*s\" size:%zu",
                entry->name_size, entry->name, out->buf_size - orig_size);

    *output_size = out->buf_size - orig_size;
    return knd_OK;
}

int knd_class_inst_export_GSP(struct kndClassInst *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    size_t curr_depth;
    int err;

    OUT(self->name, self->name_size);
    if (task->ctx->use_alias && self->alias_size) {
        err = out->write(out, "{_as ", strlen("{_as "));             RET_ERR();
        err = out->write(out, self->alias,
                         self->alias_size);                   RET_ERR();
        err = out->writec(out, '}');                                 RET_ERR();
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
        err = knd_attr_vars_export_GSP(self->class_var->attrs, out, task, 0, false);
        KND_TASK_ERR("failed to export attr vars GSP");
        task->ctx->depth = curr_depth;
    }
    return knd_OK;
}
