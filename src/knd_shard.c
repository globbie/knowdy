#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

#include <unistd.h>
#include <pthread.h>

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

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_SHARD_LEVEL_0 0
#define DEBUG_SHARD_LEVEL_1 0
#define DEBUG_SHARD_LEVEL_2 0
#define DEBUG_SHARD_LEVEL_3 0
#define DEBUG_SHARD_LEVEL_TMP 1

#define MAX_DEQUE_ATTEMPTS 100
#define TASK_TIMEOUT_USECS 500
#define TASK_QUEUE_CAPACITY 20

static void *worker_proc(void *ptr)
{
    struct kndWorker *worker = (struct kndWorker*)ptr;
    struct kndQueue *queue = worker->shard->task_context_queue;
    struct kndTaskContext *ctx;
    struct kndTask *task;
    void *elem;
    size_t attempt_count = 0;
    int err;

    knd_log(".. shard's worker #%zu..", worker->id);

    while (1) {
        attempt_count++;
        err = knd_queue_pop(queue, &elem);
        if (err) {
            if (attempt_count > MAX_DEQUE_ATTEMPTS)
                usleep(TASK_TIMEOUT_USECS);
            continue;
        }
        ctx = elem;
        ctx->phase = KND_COMPLETE;
        attempt_count = 0;

        knd_log("++ #%zu worker got task #%zu!",
                worker->id, ctx->numid);
        task = worker->task;
        task->ctx = ctx;

        err = kndTask_run(task, ctx->input, ctx->input_size, worker->shard);
        if (err != knd_OK) {
            ctx->error = err;
            knd_log("-- task running failure: %d", err);
            // report failure
        }
    }
    return NULL;
}

int knd_shard_run_task(struct kndShard *self,
                       const char *input, size_t input_size,
                       const char **task_id, size_t *task_id_size)
{
    //char buf[KND_TEMP_BUF_SIZE];
    /*clockid_t clk_id;
    clk_id = CLOCK_MONOTONIC;
    struct timespec start_ts;
    struct timespec end_ts;
    */

    /*err = clock_gettime(clk_id, &start_ts);
    strftime(buf, sizeof buf, "%D %T", gmtime(&start_ts.tv_sec));

    knd_log("UTC %s.%09ld: new task curr storage size:%zu  capacity:%zu",
            buf, start_ts.tv_nsec,
            self->task_storage->buf_size, self->task_storage->capacity);
    */

    // enqueue task
    
    return knd_OK;
}

int kndShard_run_task(struct kndShard *self,
                      const char *rec, size_t rec_size,
                      const char **result, size_t *result_size,
                      int *out_task_type,
                      size_t task_id)
{
    const char *rec_start;

    assert(task_id < self->num_workers);

    struct kndTask *task = self->tasks[task_id];
    int err;

    rec_start = task->task_out->buf + task->task_out->buf_size;
    err = task->task_out->write(task->task_out, rec, rec_size);
    if (err) {
        knd_log("-- task task_out limit reached!");
        return err;
    }

    err = kndTask_run(task, rec_start, rec_size, self);
    if (err != knd_OK) {
        task->error = err;
        knd_log("-- task running failure: %d", err);
        goto final;
    }

final:

    /* save only the successful write transaction */
    switch (task->type) {
    case KND_UPDATE_STATE:
        if (!task->error)
            break;
        // fallthrough
    default:
        /* retract last write to task_task_out */
        task->task_out->rtrim(task->task_out, rec_size);
        break;
    }

    // TODO: time calculation
    err = kndTask_build_report(task);
    if (err != knd_OK) {
        knd_log("-- task report failed: %d", err);
        return -1;
    }

    /*err = clock_gettime(clk_id, &end_ts);
    if (DEBUG_SHARD_LEVEL_TMP)
        knd_log("== task completed in %ld microsecs  [reply size:%zu]",
                (end_ts.tv_nsec - start_ts.tv_nsec) / 1000,
                task->report_size);
    */
    self->report = task->report;
    self->report_size = task->report_size;

    *result = task->report;
    *result_size = task->report_size;
    if (out_task_type) *out_task_type = task->type;

    return knd_OK;
}

int knd_shard_serve(struct kndShard *self)
{
    struct kndWorker *worker;

    for (size_t i = 0; i < self->num_workers; i++) {
        if ((worker = malloc(sizeof(struct kndWorker))) == NULL) {
            perror("-- worker allocation failed");
            return knd_NOMEM;
        }

        memset(worker, 0, sizeof(*worker));
        worker->id = i;
        worker->shard = self;
        worker->task = self->tasks[i];
        self->workers[i] = worker;

        if (pthread_create(&worker->thread, NULL, worker_proc, (void*)worker)) {
            perror("-- kndWorker thread creation failed");
            return knd_FAIL;
        }
    }

    for (size_t i = 0; i < self->num_workers; i++) {
        worker = self->workers[i];
        pthread_join(worker->thread, NULL);
    }

    return knd_OK;
}

int kndShard_new(struct kndShard **shard, const char *config, size_t config_size)
{
    struct kndShard *self;
    struct kndMemPool *mempool = NULL;
    struct kndUser *user;
    struct kndRepo *repo;
    struct kndTask *task;
    int err;

    self = malloc(sizeof(struct kndShard));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndShard));

    err = glbOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err != knd_OK) goto error;

    err = glbOutput_new(&self->log, KND_MED_BUF_SIZE);
    if (err != knd_OK) goto error;

    err = kndMemPool_new(&mempool);
    if (err != knd_OK) goto error;
    self->mempool = mempool;

    {
        err = kndShard_parse_config(self, config, &config_size, mempool);
        if (err != knd_OK) goto error;
    }

    err = mempool->alloc(mempool); 
    if (err != knd_OK) goto error;

    if (!self->num_workers) self->num_workers = 1;

    self->workers = calloc(sizeof(struct kndWorker*), self->num_workers);
    if (!self->workers) goto error;
    self->tasks = calloc(sizeof(struct kndTask*), self->num_workers);
    if (!self->tasks) goto error;

    for (size_t i = 0; i < self->num_workers; i++) {
        err = kndTask_new(&task);
        if (err != knd_OK) goto error;
        task->shard = self;
        self->tasks[i] = task;

        if (i == 0) {
            task->mempool = mempool;
            continue;
        }

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
    size_t task_queue_capacity = TASK_QUEUE_CAPACITY; //self->num_workers * mempool->num_small_pages;
    err = knd_queue_new(&self->task_context_queue, task_queue_capacity);
    if (err != knd_OK) goto error;

    if (!self->user_class_name_size) {
        self->user_class_name_size = strlen("User");
        memcpy(self->user_class_name, "User", self->user_class_name_size);
    }

    err = knd_storage_new(&self->storage, task_queue_capacity);
    if (err != knd_OK) goto error;
    
    /* system repo */
    err = kndRepo_new(&repo, mempool);
    if (err != knd_OK) goto error;
    memcpy(repo->name, "/", 1);
    repo->name_size = 1;

    repo->schema_path = self->schema_path;
    repo->schema_path_size = self->schema_path_size;

    memcpy(repo->path, self->path, self->path_size);
    repo->path_size = self->path_size;

    // TODO
    task = self->tasks[0];
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
    for (size_t i = 0; i < self->num_workers; i++) {
        task = self->tasks[i];
        task->user = user;
        task->storage = self->storage;
    }

    *shard = self;
    return knd_OK;
 error:

    kndShard_del(self);

    return err;
}

void kndShard_del(struct kndShard *self)
{
    struct kndTask *task;
    struct kndWorker *worker;

    knd_log(".. deconstructing kndShard ..");

    if (self->user)
        self->user->del(self->user);

    if (self->repo)
        knd_repo_del(self->repo);

    if (self->out)
        self->out->del(self->out);
    if (self->log)
        self->log->del(self->log);

    if (self->workers) {
        for (size_t i = 0; i < self->num_workers; i++) {
            worker = self->workers[i];

            kndTask_del(worker->task);
            free(worker);

            if (i == 0) self->mempool = NULL;
        }
        free(self->workers);
    }

    if (self->mempool)
        self->mempool->del(self->mempool);

    free(self);
}

