#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "knd_queue.h"
#include "knd_config.h"
#include "knd_task.h"
#include "knd_utils.h"
#include "knd_storage.h"

int knd_storage_put(struct kndStorage *self,
                    struct kndTask *task)
{
    struct kndTaskContext *ctx = task->ctx;
    int err;

    knd_log(".. scheduling new storage task ctx: %.*s",
            ctx->id_size, ctx->id);

    err = knd_queue_push(self->queue, (void*)task->ctx);
    if (err) return err;

    return knd_OK;
}

int knd_storage_new(struct kndStorage **storage,
                    size_t queue_capacity)
{
    struct kndStorage *self;
    int err;

    self = malloc(sizeof(struct kndStorage));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndStorage));

    err = knd_queue_new(&self->queue, queue_capacity);
    if (err) goto error;

    *storage = self;
    return knd_OK;

 error:
    // free resources
    return err;
}
