#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdatomic.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_class_inst.h"
#include "knd_attr_inst.h"

int knd_class_inst_resolve(struct kndClassInst *self,
                           struct kndTask *task)
{
    struct kndAttrInst *attr_inst;
    int err;

    self->resolving_in_progress = true;

    for (attr_inst = self->attr_insts; attr_inst; attr_inst = attr_inst->next) {
        err = knd_attr_inst_resolve(attr_inst, task);
        KND_TASK_ERR("failed to resolve the \"%.*s\" attr inst",
                     attr_inst->attr->name_size, attr_inst->attr->name);
    }

    self->is_resolved = true;
    return knd_OK;
}
