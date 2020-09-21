#include "knd_class_inst.h"
#include "knd_task.h"
#include "knd_utils.h"

#define DEBUG_CLASS_INST_GSP_LEVEL_1 0
#define DEBUG_CLASS_INST_GSP_LEVEL_2 0
#define DEBUG_CLASS_INST_GSP_LEVEL_TMP 1

int knd_class_inst_marshall(void *obj, size_t *output_size, struct kndTask *task)
{
    struct kndClassInstEntry *entry = obj;
    struct kndOutput *out = task->out;
    size_t orig_size = out->buf_size;
    //int err;
    assert(entry->inst != NULL);

    //err = knd_class_export_GSP(entry->class, task);
    //KND_TASK_ERR("failed to export class GSP");
    OUT(entry->name, entry->name_size);

    if (DEBUG_CLASS_INST_GSP_LEVEL_TMP)
        knd_log(">> GSP of class inst \"%.*s\" size:%zu",
                entry->name_size, entry->name, out->buf_size - orig_size);

    *output_size = out->buf_size - orig_size;
    return knd_OK;
}

int knd_class_inst_export_GSP(struct kndClassInst *self, struct kndTask *task)
{
    knd_log("GSP of %.*s %p", self->name_size, self->name, task);
    return knd_OK;
}
