#include "knd_class_inst.h"
#include "knd_utils.h"

int knd_class_inst_export_GSP(struct kndClassInst *self, struct kndTask *task)
{
    knd_log("GSP of %.*s %p", self->name_size, self->name, task);
    return knd_OK;
}
