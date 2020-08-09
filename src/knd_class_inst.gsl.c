#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_attr_inst.h"

static int export_inner_GSL(struct kndClassInst *self,
                            struct kndTask *task)
{
    struct kndAttrInst *attr_inst;
    int err;

    // knd_log(".. GSL export inner obj.. ");
    /* anonymous obj */
    //err = out->writec(out, '{');   RET_ERR();

    attr_inst = self->attr_insts;
    while (attr_inst) {
        err = knd_attr_inst_export(attr_inst, KND_FORMAT_GSL, task);
        if (err) {
            KND_TASK_ERR("attr inst GSL export failed: %.*s",
                         attr_inst->attr->name_size, attr_inst->attr->name);
            return err;
        }
        attr_inst = attr_inst->next;
    }
    //err = out->writec(out, '}');   RET_ERR();

    return knd_OK;
}

int knd_class_inst_export_GSL(struct kndClassInst *self,
                              bool is_list_item,
                              struct kndTask *task,
                              size_t unused_var(depth))
{
    struct kndAttrInst *attr_inst;
    struct kndOutput *out = task->out;
    int err;

    if (self->type == KND_OBJ_INNER) {
        err = export_inner_GSL(self, task);
        if (err) {
            knd_log("-- inner obj GSL export failed");
            return err;
        }
        return knd_OK;
    }

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

    /* attr_insts */
    for (attr_inst = self->attr_insts; attr_inst; attr_inst = attr_inst->next) {
        err = knd_attr_inst_export(attr_inst, KND_FORMAT_GSL, task);
        KND_TASK_ERR("export of \"%.*s\" attr_inst failed",
                     attr_inst->attr->name_size, attr_inst->attr->name);
    }

    if (!is_list_item) {
        err = out->writec(out, '}');                                                  RET_ERR();
    }

    return knd_OK;
}
