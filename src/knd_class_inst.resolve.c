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
#include "knd_attr.h"
#include "knd_class_inst.h"

#define DEBUG_INST_RESOLVE_LEVEL_1 0
#define DEBUG_INST_RESOLVE_LEVEL_TMP 1

int knd_class_inst_resolve(struct kndClassInst *self, struct kndTask *task)
{
    struct kndClass *c = self->blueprint->class;
    int err;
    self->resolving_in_progress = true;

    if (self->class_var->attrs) {
        err = knd_resolve_attr_vars(c, self->class_var, task);
        KND_TASK_ERR("failed to resolve class inst %.*s::%.*s", c->entry->name_size, c->entry->name,
                     self->name_size, self->name);
    }

    if (DEBUG_INST_RESOLVE_LEVEL_TMP)
        knd_class_inst_str(self, 0);

    self->is_resolved = true;
    return knd_OK;
}
