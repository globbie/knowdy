#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include <pthread.h>

#include <unistd.h>
#include <time.h>

#include "knd_shard.h"
#include "knd_user.h"
#include "knd_task.h"
#include "knd_dict.h"
#include "knd_queue.h"
#include "knd_set.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_mempool.h"
#include "knd_storage.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_SHARD_LEVEL_1 0
#define DEBUG_SHARD_LEVEL_TMP 1

#define _POSIX_C_SOURCE >= 199309L

static void *task_runner(void *ptr)
{
    struct kndTask *task = ptr;
    struct kndQueue *queue = task->input_queue;
    struct kndTaskContext *ctx;
    void *elem;
    size_t attempt_count = 0;
    int err;

    knd_log("\n.. shard's task runner #%zu..", task->id);

    while (1) {
        attempt_count++;
        err = knd_queue_pop(queue, &elem);
        if (err) {
            //if (attempt_count > MAX_DEQUE_ATTEMPTS)
            //    nanosleep(TASK_TIMEOUT_USECS);
            continue;
        }
        attempt_count = 0;
        ctx = elem;
        if (ctx->type == KND_STOP_STATE) {
            knd_log("\n-- shard's task runner #%zu received a stop signal..",
                    task->id);
            return NULL;
        }

        knd_log("++ #%zu worker got task #%zu!",
                task->id, ctx->numid);

        switch (ctx->phase) {
        case KND_SUBMIT:
            knd_task_reset(task);

            task->ctx = ctx;
            task->out = ctx->out;

            err = knd_task_run(task);
            if (err != knd_OK) {
                ctx->error = err;
                knd_log("-- task running failure: %d", err);
            }
            continue;
        case KND_COMPLETE:
            knd_log("\n-- task #%zu already complete",
                    ctx->numid);
            continue;
        case KND_CANCEL:
            knd_log("\n-- task #%zu was canceled",
                    ctx->numid);
            continue;
        default:
            break;
        }

        /* any other phase requires a callback execution */
        if (ctx->cb) {
            err = ctx->cb((void*)task, ctx->id, ctx->id_size, (void*)ctx);
            if (err) {
                // signal
            }
        }
    }
    return NULL;
}

/* non-blocking interface */
int knd_shard_push_task(struct kndShard *self,
                        const char *input, size_t input_size,
                        const char **task_id, size_t *task_id_size,
                        task_cb_func cb, void *obj)
{
    struct kndTaskContext *ctx;
    //clockid_t clk_id = CLOCK_MONOTONIC;
    int err;

    ctx = malloc(sizeof(struct kndTaskContext));
    if (!ctx) return knd_NOMEM;
    memset(ctx, 0, (sizeof(struct kndTaskContext)));
    ctx->external_cb = cb;
    ctx->external_obj = obj;

    ctx->input_buf = malloc(input_size + 1);
    if (!ctx->input_buf) return knd_NOMEM; 
    memcpy(ctx->input_buf, input, input_size);
    ctx->input_buf[input_size] = '\0';
    ctx->input = ctx->input_buf;
    ctx->input_size = input_size;

    self->task_count++;
    ctx->numid = self->task_count;
    knd_uid_create(ctx->numid, ctx->id, &ctx->id_size);

    //err = clock_gettime(clk_id, &ctx->start_ts);

    /*strftime(buf, sizeof buf, "%D %T", gmtime(&start_ts.tv_sec));
    knd_log("UTC %s.%09ld: new task curr storage size:%zu  capacity:%zu",
            buf, start_ts.tv_nsec,
            self->task_storage->buf_size, self->task_storage->capacity);
    */
    
    err = knd_queue_push(self->task_context_queue, (void*)ctx);
    if (err) return err;

    knd_log("++ enqueued task #%zu", ctx->numid);

    *task_id = ctx->id; 
    *task_id_size = ctx->id_size;
    return knd_OK;
}

/* blocking interface */
int knd_shard_run_task(struct kndShard *self,
                       const char *input, size_t input_size,
                       char *output, size_t *output_size)
{
    struct kndTaskContext *ctx;
    //clockid_t clk_id = CLOCK_MONOTONIC;
    size_t num_attempts = 0;
    int err;

    ctx = malloc(sizeof(struct kndTaskContext));
    if (!ctx) return knd_NOMEM;
    memset(ctx, 0, (sizeof(struct kndTaskContext)));

    //err = clock_gettime(clk_id, &ctx->start_ts);
    //if (err) return err;

    ctx->input_buf = malloc(input_size + 1);
    if (!ctx->input_buf) return knd_NOMEM; 
    memcpy(ctx->input_buf, input, input_size);
    ctx->input_buf[input_size] = '\0';
    ctx->input = ctx->input_buf;
    ctx->input_size = input_size;

    err = knd_output_new(&ctx->out, output, *output_size);
    if (err) goto final;

    *output_size = 0;

    err = knd_output_new(&ctx->log, NULL, KND_TEMP_BUF_SIZE);
    if (err) goto final;

    self->task_count++;
    ctx->numid = self->task_count;
    knd_uid_create(ctx->numid, ctx->id, &ctx->id_size);

    /*strftime(buf, sizeof buf, "%D %T", gmtime(&start_ts.tv_sec));
    knd_log("UTC %s.%09ld: new task curr storage size:%zu  capacity:%zu",
            buf, start_ts.tv_nsec,
            self->task_storage->buf_size, self->task_storage->capacity);
    */

    //err = knd_queue_push(self->storage->input_queue, (void*)ctx);
    //if (err) return err;
    ctx->phase = KND_SUBMIT;
    err = knd_queue_push(self->task_context_queue, (void*)ctx);
    if (err) return err;

    while (1) {
        //err = clock_gettime(clk_id, &ctx->end_ts);
        //if (err) goto final;

        if ((ctx->end_ts.tv_sec - ctx->start_ts.tv_sec) > TASK_MAX_TIMEOUT_SECS) {
            // signal timeout
            err = knd_task_err_export(ctx);
            if (err) goto final;
            *output_size = ctx->out->buf_size;
            break;
        }

        // TODO atomic load
        switch (ctx->phase) {
        case KND_COMPLETE:
            if (ctx->error) {
                err = knd_task_err_export(ctx);
                if (err) return err;
            }
            *output_size = ctx->out->buf_size;
            knd_log("\n== RESULT: %.*s\n== num attempts: %zu\n",
                    ctx->out->buf_size, ctx->out->buf, num_attempts);
            return knd_OK;
        default:
            break;
        }
        //nanosleep(TASK_TIMEOUT_USECS);
        num_attempts++;
    }

    return knd_OK;

 final:
    // TODO free ctx
    return err;
}

int knd_shard_serve(struct kndShard *self)
{
    struct kndTask *task;
    int err;

    for (size_t i = 0; i < self->num_tasks; i++) {
        task = self->tasks[i];
        task->id = i;

        if (pthread_create(&task->thread, NULL, task_runner, (void*)task)) {
            perror("-- kndTask thread creation failed");
            return knd_FAIL;
        }
    }

    err = knd_storage_serve(self->storage);
    if (err) return err;

    return knd_OK;
}

int knd_shard_stop(struct kndShard *self)
{
    struct kndTaskContext ctx;
    struct kndTask *task;
    int err;

    err = knd_storage_stop(self->storage);
    if (err) return err;

    memset(&ctx, 0, sizeof(struct kndTaskContext));
    ctx.type = KND_STOP_STATE;

    knd_log(".. scheduling shard stop tasks..");

    for (size_t i = 0; i < self->num_tasks * 2; i++) {
        err = knd_queue_push(self->task_context_queue, (void*)&ctx);
        if (err) return err;
    }

    for (size_t i = 0; i < self->num_tasks; i++) {
        task = self->tasks[i];
        pthread_join(task->thread, NULL);
    }

    knd_log(".. shard tasks stopped.");
    return knd_OK;
}

int knd_shard_new(struct kndShard **shard, const char *config, size_t config_size)
{
    struct kndShard *self;
    struct kndMemPool *mempool = NULL;
    struct kndUser *user;
    struct kndRepo *repo;
    struct kndTask *task;
    struct kndTaskContext *ctx;
    char *c;
    size_t chunk_size;
    int err;

    self = malloc(sizeof(struct kndShard));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndShard));

    //err = glbOutput_new(&self->out, KND_IDX_BUF_SIZE);
    //if (err != knd_OK) goto error;

    //err = glbOutput_new(&self->log, KND_MED_BUF_SIZE);
    //if (err != knd_OK) goto error;

    err = kndMemPool_new(&mempool);
    if (err != knd_OK) goto error;
    self->mempool = mempool;

    {
        err = knd_shard_parse_config(self, config, &config_size, mempool);
        if (err != knd_OK) goto error;
    }

    err = mempool->alloc(mempool); 
    if (err != knd_OK) goto error;

    err = knd_set_new(mempool, &self->ctx_idx);
    if (err != knd_OK) goto error;
    self->ctx_idx->mempool = mempool;

    if (!self->num_tasks) self->num_tasks = 1;

    self->tasks = calloc(self->num_tasks, sizeof(struct kndTask*));
    if (!self->tasks) goto error;

    for (size_t i = 0; i < self->num_tasks; i++) {
        err = knd_task_new(&task);
        if (err != knd_OK) goto error;
        self->tasks[i] = task;

        err = kndMemPool_new(&task->mempool);
        if (err != knd_OK) goto error;

        // TODO: clone function
        task->mempool->num_pages = mempool->num_pages;
        task->mempool->num_small_x4_pages = mempool->num_small_x4_pages;
        task->mempool->num_small_x2_pages = mempool->num_small_x2_pages;
        task->mempool->num_small_pages = mempool->num_small_pages;
        task->mempool->num_tiny_pages = mempool->num_tiny_pages;

        task->mempool->alloc(task->mempool);
    }

    // TODO
    size_t task_queue_capacity = TASK_QUEUE_CAPACITY; //self->num_tasks * mempool->num_small_pages;
    err = knd_queue_new(&self->task_context_queue, task_queue_capacity);
    if (err != knd_OK) goto error;

    if (!self->user_class_name_size) {
        self->user_class_name_size = strlen("User");
        memcpy(self->user_class_name, "User", self->user_class_name_size);
    }

    /* IO service */
    err = knd_storage_new(&self->storage, task_queue_capacity);
    if (err != knd_OK) goto error;
    self->storage->output_queue = self->task_context_queue;
    memcpy(self->storage->path, self->path, self->path_size);
    self->storage->path_size = self->path_size;
    self->storage->ctx_idx = self->ctx_idx;

    /* global commit filename */
    c = self->storage->commit_filename;
    memcpy(c, self->path, self->path_size);
    c += self->path_size;
    chunk_size = self->path_size;
    *c = '/';
    c++;
    chunk_size++;

    memcpy(c, "commit.log", strlen("commit.log"));
    c++;
    chunk_size += strlen("commit.log");
    self->storage->commit_filename_size = chunk_size;

    /* system repo */
    err = kndRepo_new(&repo, mempool);
    if (err != knd_OK) goto error;
    memcpy(repo->name, "/", 1);
    repo->name_size = 1;

    repo->schema_path = self->schema_path;
    repo->schema_path_size = self->schema_path_size;

    memcpy(repo->path, self->path, self->path_size);
    repo->path_size = self->path_size;

    task = self->tasks[0];
    ctx = malloc(sizeof(struct kndTaskContext));
    if (!ctx) return knd_NOMEM;
    memset(ctx, 0, (sizeof(struct kndTaskContext)));
    ctx->class_name_idx = repo->class_name_idx;
    ctx->attr_name_idx  = repo->attr_name_idx;
    ctx->proc_name_idx  = repo->proc_name_idx;
    ctx->proc_arg_name_idx  = repo->proc_arg_name_idx;
    task->ctx = ctx;

    err = knd_repo_open(repo, task);
    if (err != knd_OK) goto error;
    self->repo = repo;

    err = kndUser_new(&user, mempool);
    if (err != knd_OK) goto error;
    self->user = user;
    user->shard = self;

    user->class_name = self->user_class_name;
    user->class_name_size = self->user_class_name_size;

    user->repo_name = self->user_repo_name;
    user->repo_name_size = self->user_repo_name_size;

    user->schema_path = self->user_schema_path;
    user->schema_path_size = self->user_schema_path_size;

    err = kndUser_init(user, task);
    if (err != knd_OK) goto error;

    /* init fields in tasks */
    for (size_t i = 0; i < self->num_tasks; i++) {
        task = self->tasks[i];
        task->user = user;
        task->storage = self->storage;
        task->path = self->path;
        task->path_size = self->path_size;

        /* NB: the same queue is used as input and output */
        task->input_queue = self->task_context_queue;
        task->output_queue = self->task_context_queue;

        task->system_repo = self->repo;
    }

    *shard = self;
    return knd_OK;
 error:

    knd_shard_del(self);

    return err;
}

void knd_shard_del(struct kndShard *self)
{
    struct kndTask *task;

    knd_log(".. deconstructing kndShard ..");

    if (self->user)
        self->user->del(self->user);

    if (self->repo)
        knd_repo_del(self->repo);

    if (self->out)
        self->out->del(self->out);
    if (self->log)
        self->log->del(self->log);

    if (self->num_tasks) {
        for (size_t i = 0; i < self->num_tasks; i++) {
            task = self->tasks[i];
            knd_task_del(task);
        }
        free(self->tasks);
    }

    if (self->mempool)
        self->mempool->del(self->mempool);

    free(self);
}

