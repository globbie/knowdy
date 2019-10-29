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

int knd_class_inst_resolve(struct kndClassInst *self,
                           struct kndTask *unused_var(task))
{
    // knd_log(".. resolving class inst %.*s",
    //        self->name_size, self->name);

    self->resolving_in_progress = true;

    self->is_resolved = true;
    return knd_OK;
}
