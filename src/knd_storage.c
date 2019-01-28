#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include <unistd.h>
#include <pthread.h>

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

static void *task_runner(void *ptr)
{
    struct kndTask *task = ptr;
    struct kndQueue *queue = task->context_queue;
    struct kndTaskContext *ctx;
    size_t attempt_count = 0;
    int err;

    knd_log(".. storage manager task runner #%zu..", task->id);

    while (1) {
        attempt_count++;
        err = knd_queue_pop(queue, (void**)&ctx);
        if (err) {
            if (attempt_count > MAX_DEQUE_ATTEMPTS)
                usleep(TASK_TIMEOUT_USECS);
            continue;
        }
        ctx->phase = KND_COMPLETE;
        attempt_count = 0;

        knd_log("++ #%zu storage got task #%zu!",
                task->id, ctx->numid);
        task->ctx = ctx;

        // IO operation
        // growing buf
        // check max timeout
    }
    return NULL;
}

int knd_storage_serve(struct kndStorage *self)
{
    struct kndTask *task;
    int err;

    if (!self->num_tasks) self->num_tasks = 1;

    self->tasks = calloc(sizeof(struct kndTask*), self->num_tasks);
    if (!self->tasks) return knd_NOMEM;

    for (size_t i = 0; i < self->num_tasks; i++) {
        err = knd_task_new(&task);
        if (err != knd_OK) goto error;
        task->id = i;
        task->context_queue = self->queue;
        self->tasks[i] = task;

        if (pthread_create(&task->thread, NULL, task_runner, (void*)task)) {
            knd_log("-- storage kndTask thread creation failed");
            err = knd_FAIL;
            goto error;
        }
    }

    /*for (size_t i = 0; i < self->num_tasks; i++) {
        task = self->tasks[i];
        pthread_join(task->thread, NULL);
        }*/

    return knd_OK;

 error:
    return err;
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
