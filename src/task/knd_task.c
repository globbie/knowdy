#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_task.h"
#include "knd_steward.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_user.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_query.h"
#include "knd_commit.h"

#include <gsl-parser.h>
#include <gsl-parser/gsl_err.h>

#define DEBUG_TASK_LEVEL_0 0
#define DEBUG_TASK_LEVEL_1 0
#define DEBUG_TASK_LEVEL_2 0
#define DEBUG_TASK_LEVEL_3 0
#define DEBUG_TASK_LEVEL_TMP 1

struct LocalContext {
    struct kndRepo *repo;
    struct kndTask *task;
};

void knd_task_del(struct kndTask *self)
{
    if (self->ctx) {
        free(self->ctx);
    }
    if (self->log) {
        self->log->del(self->log);
    }
    if (self->out) {
        self->out->del(self->out);
    }
    if (self->file_out) {
        self->file_out->del(self->file_out);
    }
    free(self);
}

void knd_task_reset(struct kndTask *self)
{
    self->type = KND_TASK_DEFAULT;
    self->phase = KND_SELECTED;

    self->depth = 0;
    self->max_depth = 1;

    if (self->ctx) {
        memset(self->ctx, 0, sizeof(*self->ctx));
    }
    self->user_ctx = self->default_user_ctx;

    self->out->reset(self->out);
    self->log->reset(self->log);

    /* only operational mempool is reset,
       cache mempool remains */
    knd_mempool_reset(self->mempool);
}

static int task_err_export_JSON(struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    err = out->write(out, "{\"err\":\"", strlen("{\"err\":\""));
    if (err) return err;

    if (task->log->buf_size) {
        err = out->write(out, task->log->buf, task->log->buf_size);
        if (err) return err;
    } else {
        err = out->write(out, "internal server error", strlen("internal server error"));
        if (err) return err;
    }
    err = out->write(out, "\"", strlen("\""));
    if (err) return err;

    err = out->write(out, "}", strlen("}"));
    if (err) return err;

    return knd_OK;
}

static int task_err_export_GSP(struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;
    err = out->write(out, "{err ", strlen("{err "));                              RET_ERR();

    err = out->write(out, "{gloss ", strlen("{gloss "));                          RET_ERR();
    if (task->log->buf_size) {
        err = out->write(out, task->log->buf, task->log->buf_size);               RET_ERR();
    } else {
        err = out->write(out, "internal error", strlen("internal error"));        RET_ERR();
    }
    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

int knd_task_err_export(struct kndTask *task)
{
    int err;

    task->out->reset(task->out);

    switch (task->ctx->format) {
    case KND_FORMAT_JSON:
        err = task_err_export_JSON(task);
        RET_ERR();
        break;
    default:
        err = task_err_export_GSP(task);
        RET_ERR();
        break;
    }
    return knd_OK;
}

int knd_task_run(struct kndTask *task, const char *input, size_t input_size)
{
    size_t total_size = 0;
    gsl_err_t parser_err;

    assert (task->ctx != NULL);
    assert (task->mempool != NULL);

    //struct kndUser *user = task->user;
    struct kndOutput *out = task->out;
    int err;

    //task->user_ctx->repo = user->repo;
    //task->user_ctx->acls = user->default_acls;
    //task->user_ctx->mempool = user->mempool_write;

    task->input = input;
    task->input_size = input_size;
    task->output = NULL;
    task->output_size = 0;

    if (DEBUG_TASK_LEVEL_2) {
        size_t chunk_size = KND_TEXT_CHUNK_SIZE;
        if (task->input_size < chunk_size) chunk_size = task->input_size;
        knd_log("== INPUT {size %zu} %.*s ..",
                task->input_size, chunk_size, task->input);
    }

    struct gslTaskSpec specs[] = {
        { .name = "query",
          .name_size = strlen("query"),
          .parse = knd_query_run,
          .obj = task
        },
        { .name = "cmd",
          .name_size = strlen("cmd"),
          .parse = knd_commit_run,
          .obj = task
        }
    };

    parser_err = gsl_parse_task(task->input, &total_size, specs, sizeof specs / sizeof specs[0]);
    switch (parser_err.code) {
    case gsl_OK:
        break;
    case gsl_NO_MATCH:
        if (!task->log->buf_size) {
            KND_TASK_LOG("{tag %.*s} is not valid here, {cmd} or {query} is expected",
                         parser_err.val_size, parser_err.val);
        }
        break;
    default:
        if (!task->log->buf_size) {
            KND_TASK_LOG("unclassified server error");
            return gsl_err_to_knd_err_codes(parser_err);
        }
        out->reset(out);
        err = out->write_escaped(out, task->log->buf, task->log->buf_size);
        if (err) {
            KND_TASK_LOG("server output error");
            return gsl_err_to_knd_err_codes(parser_err);
        }
        knd_log("-- task {ctx-error %d} {gsl-err %d}",
                task->ctx->error, parser_err.code);
        task->output = out->buf;
        task->output_size = out->buf_size;
        return gsl_err_to_knd_err_codes(parser_err);
    }

    task->output = out->buf;
    task->output_size = out->buf_size;

    switch (task->role) {
    case KND_AGENT_ARBITER:
        // fall through
    case KND_AGENT_WRITER:
        if (task->file_out->buf_size) {
            task->output = task->file_out->buf;
            task->output_size = task->file_out->buf_size;
        }
    default:
        break;
    }
    return knd_OK;
}

static int task_context_new(struct kndTaskContext **result)
{
    struct kndTaskContext *ctx;
    ctx = calloc(1, sizeof(struct kndTaskContext));
    if (!ctx) return knd_NOMEM;
    *result = ctx;
    return knd_OK;
}

static int init_cache(struct kndTaskCache *c, struct kndMemConfig *memconf)
{
    int err;

    err = knd_mempool_create(&c->mempool, memconf, 2);
    if (err) goto error;

    err = knd_set_new(&c->attr_idx, KND_SET_STORE_MEMONLY, c->mempool);
    if (err) goto error;
    err = knd_dict_new(&c->attr_name_idx, KND_MEDIUM_DICT_SIZE, KND_DICT_MEMONLY, c->mempool);
    if (err) goto error;

    err = knd_set_new(&c->cls_idx, KND_SET_STORE_MEMONLY, c->mempool);
    if (err) goto error;
    err = knd_dict_new(&c->cls_name_idx, KND_MEDIUM_DICT_SIZE, KND_DICT_MEMONLY, c->mempool);
    if (err) goto error;

    err = knd_set_new(&c->str_idx, KND_SET_STORE_MEMONLY, c->mempool);
    if (err) goto error;
    err = knd_dict_new(&c->str_dict, KND_MEDIUM_DICT_SIZE, KND_DICT_MEMONLY, c->mempool);
    if (err) goto error;

    c->max_cls_entries = KND_CACHE_MAX_ITEMS;

    return knd_OK;

 error:
    // TODO free
    return err;
}

static int create_local_write_idxs(struct kndTask *task)
{
    int err;

    err = knd_set_new(&task->idxs.cls_idx, KND_SET_STORE_MEMONLY, task->mempool);
    KND_TASK_ERR("failed to create a cls idx");

    err = knd_dict_new(&task->idxs.cls_name_idx, KND_MEDIUM_DICT_SIZE, KND_DICT_MEMONLY, task->mempool);
    KND_TASK_ERR("failed to create a cls name idx");

    err = knd_dict_new(&task->idxs.attr_name_idx, KND_SMALL_DICT_SIZE, KND_DICT_MEMONLY, task->mempool);
    KND_TASK_ERR("failed to create an attr name idx");

    err = knd_set_new(&task->idxs.attr_idx, KND_SET_STORE_MEMONLY, task->mempool);
    KND_TASK_ERR("failed to create an attr idx");
    
    err = knd_dict_new(&task->idxs.str_dict, KND_MEDIUM_DICT_SIZE, KND_DICT_MEMONLY, task->mempool);
    KND_TASK_ERR("failed to create a str dict");

    err = knd_set_new(&task->idxs.str_idx, KND_SET_STORE_MEMONLY, task->mempool);
    KND_TASK_ERR("failed to create a str idx");

    err = knd_dict_new(&task->idxs.proc_name_idx, KND_SMALL_DICT_SIZE, KND_DICT_MEMONLY, task->mempool);
    KND_TASK_ERR("failed to create a proc name idx");

    err = knd_set_new(&task->idxs.proc_idx, KND_SET_STORE_MEMONLY, task->mempool);
    KND_TASK_ERR("failed to create a proc idx");

    return knd_OK;
}

void knd_task_cleanup(struct kndTask *task)
{
    knd_mempool_reset(task->mempool);

}

void knd_task_monitor(struct kndTask *task, struct kndStorage *store,
                      struct kndResourceReport *report)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndMemPoolReport memrep = { 0 };

    knd_mempool_report(mempool, &memrep);

    report->mem_usage = memrep.total_mem_usage;
    if (memrep.total_mem_usage >\
        (mempool->capacity * store->snapshot_threshold_ratio)) {
        report->mem_threshold_alert = true;
    }
}

static int task_init(struct kndTask *task,
                     struct kndMemConfig *main_memconf, struct kndMemConfig *cache_memconf)
{
    int err;

    err = knd_mempool_create(&task->mempool, main_memconf, 1);
    if (err) {
        knd_log("failed to init a task mempool for writing");
    }

    err = task_context_new(&task->ctx);
    if (err) goto error;

    err = init_cache(&task->cache, cache_memconf);
    if (err) goto error;

    switch (task->role) {
    case KND_AGENT_SYSTEM:
        // fall through
    case KND_AGENT_WRITER:
        err = create_local_write_idxs(task);
        if (err) goto error;
        break;
    default:
        break;
    }
    
    /* default user context */
    err = knd_user_context_new(&task->default_user_ctx);
    if (err) goto error;
    task->user_ctx = task->default_user_ctx;

    return knd_OK;

 error:
    return err;
}

int knd_task_new(struct kndTask **result, knd_agent_role_type role, size_t task_id,
                 struct kndMemConfig *main_memconf, struct kndMemConfig *cache_memconf,
                 struct kndSteward *steward)
{
    struct kndTask *task;
    int err;

    if (task_id == 0) {
        if (role != KND_AGENT_SYSTEM) {
            knd_log("task id should be > 0 and < %zu", KND_MAX_TASKS);
            return knd_CONFLICT;
        }
    }
    if (task_id >= KND_MAX_TASKS) {
        knd_log("task id should be less than %zu", KND_MAX_TASKS);
        return knd_CONFLICT;
    }

    task = calloc(1, sizeof(struct kndTask));
    if (!task) return knd_NOMEM;
    task->role = role;
    task->id = task_id;

    err = knd_output_new(&task->out, NULL, KND_LARGE_BUF_SIZE);
    if (err) goto error;

    err = knd_output_new(&task->log, NULL, KND_LOG_BUF_SIZE);
    if (err) goto error;

    err = knd_output_new(&task->file_out, NULL, KND_FILE_BUF_SIZE);
    if (err) goto error;

    err = task_init(task, main_memconf, cache_memconf);
    if (err) goto error;

    task->steward = steward;

    *result = task;
    return knd_OK;

 error:
    knd_task_del(task);
    return err;
}
