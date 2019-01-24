#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
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

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_SHARD_LEVEL_0 0
#define DEBUG_SHARD_LEVEL_1 0
#define DEBUG_SHARD_LEVEL_2 0
#define DEBUG_SHARD_LEVEL_3 0
#define DEBUG_SHARD_LEVEL_TMP 1

int kndShard_run_task(struct kndShard *self,
                      const char *rec, size_t rec_size,
                      const char **result, size_t *result_size,
                      int *out_task_type,
                      size_t task_id)
{
    const char *rec_start;

    assert(task_id < self->num_workers);

    struct kndTask *task = self->workers[task_id];
    int err;

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

    rec_start = task->storage->buf + task->storage->buf_size;
    err = task->storage->write(task->storage, rec, rec_size);
    if (err) {
        knd_log("-- task storage limit reached!");
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
        /* retract last write to task_storage */
        task->storage->rtrim(task->storage, rec_size);
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
    self->workers = calloc(sizeof(struct kndTask*), self->num_workers);
    if (!self->workers) goto error;

    for (size_t i = 0; i < self->num_workers; i++) {
        err = kndTask_new(&task);
        if (err != knd_OK) goto error;
        task->shard = self;
        self->workers[i] = task;

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
    size_t task_queue_capacity = 10; //self->num_workers * mempool->num_small_pages;
    err = knd_queue_new(&self->task_queue, task_queue_capacity);
    if (err != knd_OK) goto error;

    if (!self->user_class_name_size) {
        self->user_class_name_size = strlen("User");
        memcpy(self->user_class_name, "User", self->user_class_name_size);
    }

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
    task = self->workers[0];
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

    for (size_t i = 0; i < self->num_workers; i++) {
        task = self->workers[i];
        task->user = user;
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
            task = self->workers[i];
            kndTask_del(task);
            if (i == 0) self->mempool = NULL;
        }
        free(self->workers);
    }

    if (self->mempool)
        self->mempool->del(self->mempool);

    free(self);
}

