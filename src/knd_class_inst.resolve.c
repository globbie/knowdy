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
#define DEBUG_INST_RESOLVE_LEVEL_2 0
#define DEBUG_INST_RESOLVE_LEVEL_3 0
#define DEBUG_INST_RESOLVE_LEVEL_TMP 1

int knd_class_inst_resolve(struct kndClassInst *self, struct kndTask *task)
{
    assert(self->entry->blueprint != NULL);
    struct kndClass *c;
    int err;
    self->resolving_in_progress = true;

    err = knd_class_acquire(self->entry->blueprint, &c, task);
    KND_TASK_ERR("failed to acquire class \"%.*s\"",
                 self->entry->blueprint->name_size, self->entry->blueprint->name);

    if (DEBUG_INST_RESOLVE_LEVEL_3) {
        knd_log(".. resolving {class %.*s {inst %.*s}}",
                c->entry->name_size, c->entry->name,
                self->name_size, self->name);
    }

    if (self->class_var->attrs) {
        err = knd_resolve_attr_vars(c, self->class_var, task);
        KND_TASK_ERR("failed to resolve {class %.*s {inst %.*s}}",
                     c->entry->name_size, c->entry->name,
                     self->name_size, self->name);
    }
    self->is_resolved = true;
    return knd_OK;
}
