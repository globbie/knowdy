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
#include "knd_class.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>
#include <gsl-parser/gsl_err.h>

#define DEBUG_TASK_LEVEL_0 0
#define DEBUG_TASK_LEVEL_1 0
#define DEBUG_TASK_LEVEL_2 0
#define DEBUG_TASK_LEVEL_3 0
#define DEBUG_TASK_LEVEL_TMP 1

void knd_task_del(struct kndTask *self)
{
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
    self->type = KND_GET_STATE;
    self->phase = KND_SELECTED;
    /* initialize request with off limit values */
    self->state_eq = -1;
    self->state_gt = -1;
    self->state_gte = -1;
    self->state_lt = 0;
    self->state_lte = 0;

    self->show_removed_objs = false;
    self->depth = 0;
    self->max_depth = 1;

    if (self->ctx)
        memset(self->ctx, 0, sizeof(*self->ctx));

    self->user_ctx = self->default_user_ctx;
    self->repo = self->system_repo;

    self->payload = NULL;
    self->out->reset(self->out);
    self->log->reset(self->log);

    if (self->mempool)
        knd_mempool_reset(self->mempool);

    // NB self->cache_mempool stays intact

    if (self->class_name_idx)
        knd_dict_reset(self->class_name_idx);

    if (self->class_inst_alias_idx)
        knd_dict_reset(self->class_inst_alias_idx);

    if (self->attr_name_idx)
        knd_dict_reset(self->attr_name_idx);

    if (self->proc_name_idx)
        knd_dict_reset(self->proc_name_idx);

    if (self->proc_arg_name_idx)
        knd_dict_reset(self->proc_arg_name_idx);
}

static int task_err_export_JSON(struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndTaskContext *ctx = task->ctx;
    int err;

    err = out->write(out, "{\"err\":\"", strlen("{\"err\":\""));
    if (err) return err;

    if (task->log->buf_size) {
        err = out->write(out, task->log->buf, task->log->buf_size);
        if (err) return err;
    } else {
        ctx->http_code = HTTP_INTERNAL_SERVER_ERROR;
        err = out->write(out, "internal server error", strlen("internal server error"));
        if (err) return err;
    }
    err = out->write(out, "\"", strlen("\""));
    if (err) return err;

    if (ctx->http_code != HTTP_OK) {
        err = out->write(out, ",\"http_code\":", strlen(",\"http_code\":"));
        if (err) return err;
        err = out->writef(out, "%d", ctx->http_code);
        if (err) return err;
    } else {
        ctx->http_code = HTTP_NOT_FOUND;
        // convert error code to HTTP error
        err = out->write(out, ",\"http_code\":", strlen(",\"http_code\":"));
        if (err) return err;
        err = out->writef(out, "%d", HTTP_NOT_FOUND);
        if (err) return err;
    }
    err = out->write(out, "}", strlen("}"));
    if (err) return err;

    return knd_OK;
}

static int task_err_export_GSP(struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndTaskContext *ctx = task->ctx;
    int err;
    err = out->write(out, "{err ", strlen("{err "));                              RET_ERR();
    err = out->writef(out, "%d", ctx->http_code);                                RET_ERR();

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
        err = task_err_export_JSON(task);                                    RET_ERR();
        break;
    default:
        err = task_err_export_GSP(task);                                     RET_ERR();
        break;
    }
    return knd_OK;
}

int knd_task_run(struct kndTask *task, const char *input, size_t input_size)
{
    size_t total_size = 0;
    gsl_err_t parser_err;

    assert (task->steward != NULL);
    assert (task->ctx != NULL);

    struct kndUser *user = task->steward->user;
    struct kndOutput *out = task->out;
    int err;

    task->user_ctx->repo = user->repo;
    task->user_ctx->acls = user->default_acls;
    task->user_ctx->mempool = user->mempool_write;

    task->input = input;
    task->input_size = input_size;
    task->output = NULL;
    task->output_size = 0;

    if (DEBUG_TASK_LEVEL_2) {
        size_t chunk_size = KND_TEXT_CHUNK_SIZE;
        if (task->input_size < chunk_size) chunk_size = task->input_size;
        knd_log("== INPUT (size:%zu): %.*s ..",
                task->input_size, chunk_size, task->input);
    }

    struct gslTaskSpec specs[] = {
        { .name = "task",
          .name_size = strlen("task"),
          .parse = knd_parse_task,
          .obj = task
        }
    };
    parser_err = gsl_parse_task(task->input, &total_size, specs, sizeof specs / sizeof specs[0]);
    switch (parser_err.code) {
    case gsl_OK:
        break;
    case gsl_NO_MATCH:
        if (!task->log->buf_size) {
            KND_TASK_LOG("\"%.*s\" tag is not valid here, \"task\" expected",
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
        knd_log("-- task {ctx-error %d} {gls-err %d}",
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
    ctx = calloc(1, sizeof(struct kndUserContext));
    if (!ctx) return knd_NOMEM;
    *result = ctx;
    return knd_OK;
}

int knd_task_fetch_memblock(struct kndTask *task,
                            size_t space_required, struct kndMemBlock **result)
{
    struct kndMemBlock *block, *curr_block;
    int err;

    if (space_required >= KND_MEMBLOCK_BUF_SIZE) return knd_LIMIT;

    if (!task->blocks) {
        err = knd_memblock_new(&block, 0, KND_MEMBLOCK_BUF_SIZE);
        KND_TASK_ERR("failed to alloc a memblock");
        *result = block;
        return knd_OK;
    }

    curr_block = task->blocks;
    if ((curr_block->capacity - curr_block->buf_size) >= space_required) {
        *result = curr_block;
        return knd_OK;
    }

    err = knd_memblock_new(&block, 0, KND_MEMBLOCK_BUF_SIZE);
    KND_TASK_ERR("failed to alloc a memblock");
    block->next = curr_block;
    task->blocks = block;
    task->num_blocks++;
    *result = block;
    return knd_OK;
}

void knd_task_cleanup(struct kndTask *task, struct kndSteward *steward)
{
    assert (steward != NULL);
    assert (steward->repo != NULL);

    knd_mempool_reset(task->mempool);
    knd_mempool_reset(task->cache_mempool);

    task->user_ctx = task->default_user_ctx;
    task->user_ctx->mempool = steward->mempool_write;
    task->user_ctx->repo = steward->repo;

    if (steward->user) {
        task->user_ctx->mempool = steward->user->mempool_write;
        task->user_ctx->repo = steward->user->repo;
        task->user_ctx->acls = steward->user->default_acls;
    }
}

int knd_task_init(struct kndTask *task, struct kndSteward *steward)
{
    assert (steward != NULL);
    assert (steward->repo != NULL);

    struct kndRepo *repo = steward->repo;
    struct kndMemPool *mempool;
    struct kndOutput *out = steward->out;
    struct kndOutput *log = steward->log;
    int err;

    task->steward = steward;
    task->path = steward->path;
    task->path_size = steward->path_size;

    err = knd_mempool_create(&task->ctx_mempool, &steward->mem_ctx_config, 1);
    KND_STEWARD_ERR("failed to init a local ctx mempool for writing");

    err = knd_mempool_create(&task->ctx_cache_mempool, &steward->mem_ctx_config, 2);
    KND_STEWARD_ERR("failed to init a local ctx cache mempool");

    /* local name indices */
    mempool = task->ctx_mempool;

    err = knd_dict_new(&task->class_name_idx, mempool, KND_SMALL_DICT_SIZE);
    if (err) goto error;
    err = knd_dict_new(&task->class_inst_alias_idx, mempool, KND_SMALL_DICT_SIZE);
    if (err) goto error;

    err = knd_dict_new(&task->attr_name_idx, mempool, KND_SMALL_DICT_SIZE);
    if (err) goto error;
    err = knd_dict_new(&task->proc_name_idx, mempool, KND_SMALL_DICT_SIZE);
    if (err) goto error;
    err = knd_dict_new(&task->proc_arg_name_idx, mempool, KND_SMALL_DICT_SIZE);
    if (err) goto error;


    /* local cache */
    mempool = task->ctx_cache_mempool;
    err = knd_set_new(&task->cache_class_idx, mempool);
    if (err) goto error;

    /* system repo defaults */
    task->system_repo       = repo;
    task->repo              = repo;

    err = task_context_new(&task->ctx);
    if (err) goto error;

    /* default user context */
    err = knd_user_context_new(&task->default_user_ctx);
    if (err) goto error;
    task->user_ctx = task->default_user_ctx;
    task->user_ctx->mempool = steward->mempool_write;
    task->user_ctx->repo = steward->repo;

    if (steward->user) {
        task->user_ctx->mempool = steward->user->mempool_write;
        task->user_ctx->repo = steward->user->repo;
        task->user_ctx->acls = steward->user->default_acls;
    }
    return knd_OK;

 error:
    return err;
}

int knd_task_new(struct kndTask **result,
                 knd_agent_role_type role, int task_id, struct kndSteward *steward)
{
    struct kndTask *task;
    int err;

    if (task_id == 0) {
        if (role != KND_AGENT_AUX) {
            knd_log("task id should be > 0 and < %zu", KND_MAX_TASKS);
            return knd_CONFLICT;
        }
    }
    if (task_id >= KND_MAX_TASKS || task_id < 0) {
        knd_log("task id should be > 0 and < %zu", KND_MAX_TASKS);
        return knd_CONFLICT;
    }

    task = calloc(1, sizeof(struct kndTask));
    if (!task) return knd_NOMEM;
    task->role = role;
    task->id = task_id;

    err = knd_output_new(&task->out, NULL, KND_LARGE_BUF_SIZE);
    if (err) goto error;

    err = knd_output_new(&task->log, NULL, KND_TEMP_BUF_SIZE);
    if (err) goto error;

    err = knd_output_new(&task->file_out, NULL, KND_FILE_BUF_SIZE);
    if (err) goto error;

    err = knd_task_init(task, steward);
    if (err) goto error;
    
    *result = task;
    return knd_OK;

 error:
    knd_task_del(task);
    return err;
}
