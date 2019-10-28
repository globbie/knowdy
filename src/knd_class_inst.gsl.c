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

    /* anonymous obj */
    //err = out->writec(out, '{');   RET_ERR();

    attr_inst = self->attr_insts;
    while (attr_inst) {
        err = knd_attr_inst_export(attr_inst, KND_FORMAT_GSL, task);
        if (err) {
            knd_log("-- inst attr_inst GSL export failed: %.*s",
                    attr_inst->attr->name_size, attr_inst->attr->name);
            return err;
        }
        attr_inst = attr_inst->next;
    }
    //err = out->writec(out, '}');   RET_ERR();

    return knd_OK;
}

int knd_class_inst_export_GSL(struct kndClassInst *self, struct kndTask *task)
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

    err = out->write(out, self->name, self->name_size);   RET_ERR();
    err = out->write(out, "{_id ", strlen("{_id "));      RET_ERR();
    err = out->write(out, self->entry->id,
                          self->entry->id_size);       RET_ERR();
    err = out->writec(out, '}');                          RET_ERR();
   
    /* attr_insts */
    for (attr_inst = self->attr_insts; attr_inst; attr_inst = attr_inst->next) {
        err = knd_attr_inst_export(attr_inst, KND_FORMAT_GSL, task);
        if (err) {
            knd_log("-- export of \"%s\" attr_inst failed: %d :(",
                    attr_inst->attr->name, err);
            return err;
        }
    }
    return knd_OK;
}
