#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_attr.h"

int knd_class_inst_export_GSL(struct kndClassInst *self, bool is_list_item, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    size_t curr_depth;
    int err;

    if (!is_list_item) {
        err = out->writec(out, '{');                                              RET_ERR();
        err = out->write(out, "inst ", strlen("inst "));                          RET_ERR();
    }

    err = out->write(out, self->name, self->name_size);              RET_ERR();
    if (task->ctx->use_numid) {
        err = out->write(out, "{_id ", strlen("{_id "));             RET_ERR();
        err = out->write(out, self->entry->id,
                         self->entry->id_size);                      RET_ERR();
        err = out->writec(out, '}');                                 RET_ERR();
    }
    if (task->ctx->use_alias && self->alias_size) {
        err = out->write(out, "{_as ", strlen("{_as "));             RET_ERR();
        err = out->write(out, self->alias,
                         self->alias_size);                   RET_ERR();
        err = out->writec(out, '}');                                 RET_ERR();
    }

    if (self->class_var->attrs) {
        curr_depth = task->ctx->depth;
        err = knd_attr_vars_export_GSL(self->class_var->attrs, task, false, depth + 1);  RET_ERR();
        task->ctx->depth = curr_depth;   
    }

    if (!is_list_item) {
        err = out->writec(out, '}');                                                  RET_ERR();
    }

    return knd_OK;
}
