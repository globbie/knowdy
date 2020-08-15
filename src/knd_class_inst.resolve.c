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

#define DEBUG_INST_RESOLVE_LEVEL_1 0
#define DEBUG_INST_RESOLVE_LEVEL_TMP 1

int knd_class_inst_resolve(struct kndClassInst *self,
                           struct kndTask *task)
{
    int err;
    self->resolving_in_progress = true;

    if (self->class_var->attrs) {
        err = knd_resolve_attr_vars(self->blueprint, self->class_var, task);   RET_ERR();
    }

    if (DEBUG_INST_RESOLVE_LEVEL_TMP)
        knd_class_inst_str(self, 0);

    self->is_resolved = true;
    return knd_OK;
}
