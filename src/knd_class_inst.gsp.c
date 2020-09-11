#include "knd_class_inst.h"
#include "knd_task.h"
#include "knd_utils.h"

int knd_class_inst_marshall_GSP(void *obj, size_t *output_size, struct kndTask *task)
{
    struct kndClassInstEntry *entry = obj;
    int err = knd_FAIL;

    knd_log(".. marshall class inst %.*s", entry->name_size, entry->name);
    KND_TASK_ERR("failed to marshall class inst");

    *output_size = 0;
    return knd_OK;
}

int knd_class_inst_export_GSP(struct kndClassInst *self, struct kndTask *task)
{
    knd_log("GSP of %.*s %p", self->name_size, self->name, task);
    return knd_OK;
}
