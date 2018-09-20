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
#include "knd_mempool.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_SHARD_LEVEL_0 0
#define DEBUG_SHARD_LEVEL_1 0
#define DEBUG_SHARD_LEVEL_2 0
#define DEBUG_SHARD_LEVEL_3 0
#define DEBUG_SHARD_LEVEL_TMP 1

void kndShard_del(struct kndShard *self)
{
    knd_log(".. deconstructing kndShard ..");

    self->user->del(self->user);

    free(self);
}

static gsl_err_t
parse_memory_settings(void *obj, const char *rec, size_t *total_size)
{
    struct kndMemPool *mempool = obj;
    return mempool->parse(mempool, rec, total_size);
}

static int kndShard_run_task(struct kndShard *self,
                             const char *rec,
                             size_t rec_size,
                             char *result  __attribute__((unused)),
                             size_t *result_size  __attribute__((unused)))
{
    char buf[KND_TEMP_BUF_SIZE];
    const char *rec_start;

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

    err = self->task->run(self->task,
                          rec_start, rec_size,
                          "None", sizeof("None"));
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

    return knd_OK;
}

static gsl_err_t
run_check_schema(void *obj __attribute__((unused)), const char *val, size_t val_size)
{
    const char *schema_name = "knd";
    size_t schema_name_size = strlen(schema_name);

    if (val_size != schema_name_size)  return make_gsl_err(gsl_FAIL);
    if (memcmp(schema_name, val, val_size)) return make_gsl_err(gsl_FAIL);
    return make_gsl_err(gsl_OK);
}


static gsl_err_t
kndShard_parse_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndShard *self = obj;
    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = run_check_schema,
            .obj = self
        },
        {   .name = "path",
            .name_size = strlen("path"),
            .buf = self->path,
            .buf_size = &self->path_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {   .name = "user",
            .name_size = strlen("user"),
            .buf = self->user_classname,
            .buf_size = &self->user_classname_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {   .name = "schemas",
            .name_size = strlen("schemas"),
            .buf = self->schema_path,
            .buf_size = &self->schema_path_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {   .name = "sid",
            .name_size = strlen("sid"),
            .buf = self->sid,
            .buf_size = &self->sid_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {  .name = "memory",
            .name_size = strlen("memory"),
            .parse = parse_memory_settings,
            .obj = self->mempool,
        },
        {   .name = "agent",
            .name_size = strlen("agent"),
            .buf = self->name,
            .buf_size = &self->name_size,
            .max_buf_size = KND_NAME_SIZE
        }
    };

    int err = knd_FAIL;
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return parser_err;
    }

    if (!self->path_size) {
        knd_log("-- DB path not set :(");
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_mkpath(self->path, self->path_size, 0755, false);
    if (err != knd_OK) return make_gsl_err_external(err);

    if (!self->schema_path_size) {
        knd_log("-- schema path not set :(");
        return make_gsl_err(gsl_FAIL);
    }

    if (!self->sid_size) {
        knd_log("-- root SID is not set :(");
        return make_gsl_err(gsl_FAIL);
    }

    return make_gsl_err(gsl_OK);
}

static int
parse_schema(struct kndShard *self, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        {
            .name = "schema",
            .name_size = sizeof("schema") - 1,
            .parse = kndShard_parse_config,
            .obj = self
        }
    };

    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code != gsl_OK) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

extern int kndShard_new(struct kndShard **shard,
                        const char *config_filename)
{
    struct kndShard *self;
    struct kndMemPool *mempool;
    struct kndUser *user;
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

    { // read config
        size_t chunk_size;
        err = self->out->write_file_content(self->out,
                                            config_filename);
        if (err != knd_OK) goto error;

        err = parse_schema(self, self->out->buf, &chunk_size);
        if (err != knd_OK) goto error;
    }

    err = mempool->alloc(mempool);                           RET_ERR();
    mempool->log = self->task->log;

    /* set default user class name */
    if (!self->user_classname_size) {
        self->user_classname_size = strlen("User");
        memcpy(self->user_classname, "User", self->user_classname_size);
    }

    err = kndUser_new(&user, mempool);
    if (err != knd_OK) goto error;
    self->user = user;
    user->shard = self;

    err = user->init(user);
    if (err != knd_OK) goto error;

    self->del = kndShard_del;
    self->run_task = kndShard_run_task;

    *shard = self;
    return knd_OK;
 error:
    // TODO: release resources
    return err;
}
