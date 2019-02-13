#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include "knd_queue.h"
#include "knd_config.h"
#include "knd_task.h"
#include "knd_utils.h"
#include "knd_storage.h"
#include "knd_repo.h"
#include "knd_set.h"

static int wal_write(struct kndTask *task,
                     struct kndTaskContext *unused_var(ctx))
{
    knd_log(".. kndStorage to write a WAL entry.. (commit file:%s)",
            task->filename);

    
    return knd_OK;
}

static int wal_commit(struct kndTask *task,
                      struct kndTaskContext *ctx)
{
    struct kndRepo   *repo = ctx->repo;
    struct kndUpdate *update = ctx->update;
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    char *c;
    int err;

    c = buf;
    *c = '{';
    c++;
    buf_size = 1;

    memcpy(c, repo->name, repo->name_size);
    c += repo->name_size;
    buf_size += repo->name_size;

    *c = '{';
    c++;
    buf_size++;

    memcpy(c, update->id, update->id_size);
    c += update->id_size;
    buf_size += update->id_size;

    memcpy(c, "}}", 2);
    c += 2;
    *c = '\n';
    buf_size += 3;

    //knd_log(".. kndStorage (%s) to confirm commit %zu",
    //        task->filename, ctx->numid);

    err = knd_append_file(task->filename,
                          buf, buf_size);
    if (err) {
        knd_log("-- commit write failure: %d", err);
        return err;
    }
    update->confirm = KND_VALID_STATE;

    return knd_OK;
}


static void *task_runner(void *ptr)
{
    struct kndTask *task = ptr;
    struct kndQueue *queue =        task->input_queue;
    struct kndQueue *output_queue = task->output_queue;
    struct kndTaskContext *ctx;
    struct timespec ts = {0, TASK_TIMEOUT_USECS * 1000L };
    size_t attempt_count = 0;
    int err;

    knd_log(".. storage manager task runner #%zu..", task->id);

    while (1) {
        attempt_count++;
        err = knd_queue_pop(queue, (void**)&ctx);
        if (err) {
            if (attempt_count > MAX_DEQUE_ATTEMPTS)
                nanosleep(&ts, NULL);
            continue;
        }

        switch (ctx->type) {
        case KND_STOP_STATE:
            knd_log("\n-- storage task runner #%zu received a stop signal",
                    task->id);
            return NULL;
        default:
            break;
        }

        attempt_count = 0;

        //knd_log("++ #%zu storage (path: %.*s) got task #%zu  (phase:%d)",
        //        task->id, task->path_size, task->path, ctx->numid, ctx->phase);

        switch (ctx->phase) {
        case KND_REGISTER:
            err = task->ctx_idx->add(task->ctx_idx,
                                     ctx->id, ctx->id_size, (void*)ctx);
            if (err) {
                // signal
            }
            ctx->phase = KND_SUBMIT;
            break;
        case KND_CANCEL:
            knd_log("\n-- storage task #%zu was canceled",
                    ctx->numid);
            continue;
        case KND_COMPLETE:
            knd_log("\n-- storage task #%zu is already complete?",
                    ctx->numid);
            continue;
        case KND_WAL_WRITE:
            err = wal_write(task, ctx);
            if (err) {
                ctx->error = err;
                // signal
            }
            break;
        case KND_WAL_COMMIT:

            /* check conflicts */
            err = knd_repo_check_conflicts(ctx->repo, ctx);
            if (err) {
                ctx->error = err;
                // signal
                break;
            }

            /* update in-memory repo name idx */
            err = knd_repo_update_name_idx(ctx->repo, ctx);
            if (err) {
                ctx->error = err;
                // signal
            }

            err = wal_commit(task, ctx);
            if (err) {
                ctx->error = err;
                // NB: service MUST stop accepting transactions here
                // raise alert to solve this problem
            }

            /* persistent commit confirmed */

            break;
        default:
            break;
        }

        task->ctx = ctx;

        // TODO growing buf
        // check max timeout

        err = knd_queue_push(output_queue, (void*)ctx);
        if (err) {
            // signal error
        }
    }
    return NULL;
}

int knd_storage_serve(struct kndStorage *self)
{
    struct kndTask *task;
    int err;

    if (!self->num_tasks) self->num_tasks = 1;

    self->tasks = calloc(self->num_tasks, sizeof(struct kndTask*));
    if (!self->tasks) return knd_NOMEM;

    for (size_t i = 0; i < self->num_tasks; i++) {
        err = knd_task_new(&task);
        if (err != knd_OK) goto error;
        task->id = i;
        task->input_queue = self->input_queue;
        task->output_queue = self->output_queue;

        task->path = self->path;
        task->path_size = self->path_size;

        task->filename = self->commit_filename;
        task->filename_size = self->commit_filename_size;
        task->ctx_idx = self->ctx_idx;

        self->tasks[i] = task;

        if (pthread_create(&task->thread, NULL, task_runner, (void*)task)) {
            knd_log("-- storage kndTask thread creation failed");
            err = knd_FAIL;
            goto error;
        }
    }

    return knd_OK;

 error:
    return err;
}

int knd_storage_stop(struct kndStorage *self)
{
    struct kndTask *task;
    struct kndTaskContext ctx;
    int err;

    memset(&ctx, 0, sizeof(struct kndTaskContext));
    ctx.type = KND_STOP_STATE;

    knd_log(".. scheduling storage stop tasks..");

    for (size_t i = 0; i < self->num_tasks * 2; i++) {
        err = knd_queue_push(self->input_queue, (void*)&ctx);
        if (err) return err;
    }

    for (size_t i = 0; i < self->num_tasks; i++) {
        task = self->tasks[i];
        pthread_join(task->thread, NULL);
    }

    knd_log(".. storage tasks stopped.");
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

    err = knd_queue_new(&self->input_queue, queue_capacity);
    if (err) goto error;

    *storage = self;
    return knd_OK;

 error:
    // free resources
    return err;
}
