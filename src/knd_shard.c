#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

#include "knd_shard.h"
#include "knd_repo.h"
#include "knd_user.h"
#include "knd_task.h"
#include "knd_dict.h"
#include "knd_set.h"
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

int kndShard_run_task(struct kndShard *self, const char *rec, size_t rec_size,
                             const char **result, size_t *result_size)
{
    const char *rec_start;

    //char buf[KND_TEMP_BUF_SIZE];
    /*clockid_t clk_id;
    clk_id = CLOCK_MONOTONIC;
    struct timespec start_ts;
    struct timespec end_ts;
    */

    int err;

    /*err = clock_gettime(clk_id, &start_ts);
    strftime(buf, sizeof buf, "%D %T", gmtime(&start_ts.tv_sec));

    knd_log("UTC %s.%09ld: new task curr storage size:%zu  capacity:%zu",
            buf, start_ts.tv_nsec,
            self->task_storage->buf_size, self->task_storage->capacity);

    */

    rec_start = self->task_storage->buf + self->task_storage->buf_size;
    err = self->task_storage->write(self->task_storage, rec, rec_size);
    if (err) {
        knd_log("-- task storage limit reached!");
        return err;
    }

    err = self->task->run(self->task, rec_start, rec_size, "None", sizeof("None"));
    if (err != knd_OK) {
        self->task->error = err;
        knd_log("-- task running failure: %d", err);
        goto final;
    }

final:

    /* save only the successful write transaction */
    switch (self->task->type) {
    case KND_UPDATE_STATE:
        if (!self->task->error)
            break;
    default:
        /* retract last write to task_storage */
        self->task_storage->rtrim(self->task_storage, rec_size);
        break;
    }

    // TODO: time calculation
    err = self->task->build_report(self->task);
    if (err != knd_OK) {
        knd_log("-- task report failed: %d", err);
        return -1;
    }

    /*err = clock_gettime(clk_id, &end_ts);
    if (DEBUG_SHARD_LEVEL_TMP)
        knd_log("== task completed in %ld microsecs  [reply size:%zu]",
                (end_ts.tv_nsec - start_ts.tv_nsec) / 1000,
                self->task->report_size);
    */
    self->report = self->task->report;
    self->report_size = self->task->report_size;

    *result = self->task->report;
    *result_size = self->task->report_size;

    return knd_OK;
}

int kndShard_new(struct kndShard **shard, const char *config, size_t config_size)
{
    struct kndShard *self;
    struct kndMemPool *mempool;
    struct kndUser *user;
    struct kndRepo *repo;
    int err;

    self = malloc(sizeof(struct kndShard));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndShard));

    err = glbOutput_new(&self->task_storage, KND_TASK_STORAGE_SIZE);
    if (err != knd_OK) goto error;

    err = glbOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err != knd_OK) goto error;

    err = glbOutput_new(&self->log, KND_MED_BUF_SIZE);
    if (err != knd_OK) goto error;

    err = kndTask_new(&self->task);
    if (err != knd_OK) goto error;
    self->task->shard = self;

    err = kndMemPool_new(&mempool);
    if (err != knd_OK) return err;
    self->mempool = mempool;
    {
        err = kndShard_parse_schema(self, config, &config_size);
        if (err != knd_OK) goto error;
    }

    err = mempool->alloc(mempool);                                                RET_ERR();
    mempool->log = self->task->log;

    if (!self->user_class_name_size) {
        self->user_class_name_size = strlen("User");
        memcpy(self->user_class_name, "User", self->user_class_name_size);
    }

    /* system repo */
    err = kndRepo_new(&repo, mempool);                                            RET_ERR();
    memcpy(repo->name, "/", 1);
    repo->name_size = 1;
    repo->task = self->task;
    repo->schema_path = self->schema_path;
    repo->schema_path_size = self->schema_path_size;

    memcpy(repo->path, self->path, self->path_size);
    repo->path_size = self->path_size;

    err = repo->init(repo);                                                       RET_ERR();
    self->repo = repo;

    err = kndUser_new(&user, mempool);
    if (err != knd_OK) goto error;
    self->user = user;
    user->shard = self;
    user->task = self->task;
    user->out = self->out;
    user->log = self->log;

    user->class_name = self->user_class_name;
    user->class_name_size = self->user_class_name_size;

    user->repo_name = self->user_repo_name;
    user->repo_name_size = self->user_repo_name_size;

    user->schema_path = self->user_schema_path;
    user->schema_path_size = self->user_schema_path_size;

    err = user->init(user);
    if (err != knd_OK) goto error;

    *shard = self;
    return knd_OK;
 error:

    kndShard_del(self);

    return err;
}

void kndShard_del(struct kndShard *self)
{
    knd_log(".. deconstructing kndShard ..");

    self->user->del(self->user);

    knd_log(".. del sys repo..");
    self->repo->del(self->repo);

    knd_log(".. del shard out ..");
    self->out->del(self->out);
    knd_log(".. del shard log ..");
    self->log->del(self->log);

    self->task_storage->del(self->task_storage);

    knd_log(".. del task ..");
    self->task->del(self->task);
    
    knd_log(".. del mempool ..");
    self->mempool->del(self->mempool);

    knd_log(".. del shard self: %p", self);

    free(self);
}

