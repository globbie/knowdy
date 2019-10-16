#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_task.h"
#include "knd_shard.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_user.h"
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
    self->log->del(self->log);
    self->out->del(self->out);
    self->file_out->del(self->file_out);
    if (self->is_mempool_owner)
        knd_mempool_del(self->mempool);
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

    self->http_code = HTTP_OK;

    if (self->ctx)
        memset(self->ctx, 0, sizeof(*self->ctx));

    self->user_ctx = NULL;
    self->repo = self->system_repo;

    self->out->reset(self->out);
    self->log->reset(self->log);

    if (self->role == KND_READER)
        knd_mempool_reset(self->mempool);

    knd_dict_reset(self->class_name_idx);
    knd_dict_reset(self->attr_name_idx);
    knd_dict_reset(self->proc_name_idx);
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

    assert(task->ctx != NULL);

    task->input = input;
    task->input_size = input_size;

    task->output = NULL;
    task->output_size = 0;

    if (DEBUG_TASK_LEVEL_TMP) {
        size_t chunk_size = KND_TEXT_CHUNK_SIZE;
        if (task->input_size < chunk_size)
            chunk_size = task->input_size;
        knd_log("== INPUT (size:%zu): %.*s ..",
                task->input_size,
                chunk_size, task->input);
    }

    struct gslTaskSpec specs[] = {
        { .name = "task",
          .name_size = strlen("task"),
          .parse = knd_parse_task,
          .obj = task
        }
    };
    parser_err = gsl_parse_task(task->input, &total_size,
                                specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        if (!task->log->buf_size) {
            task->http_code = HTTP_INTERNAL_SERVER_ERROR;
            KND_TASK_LOG("unclassified server error");
        }
        return gsl_err_to_knd_err_codes(parser_err);
    }
    task->output = task->out->buf;
    task->output_size = task->out->buf_size;
    return knd_OK;
}

int knd_task_copy_block(struct kndTask *task,
                        const char *input, size_t input_size,
                        const char **output, size_t *output_size)
{
    int err;
    struct kndMemBlock *block = malloc(sizeof(struct kndMemBlock));
    if (!block) {
        err = knd_NOMEM;
        KND_TASK_ERR("block alloc failed");
    }

    char *b = malloc(input_size + 1);
    if (!b) {
        err = knd_NOMEM;
        KND_TASK_ERR("block alloc failed");
    }

    memcpy(b, input, input_size);
    b[input_size] = '\0';

    block->tid = 0;
    block->buf = b;
    block->buf_size = input_size;
    block->next = task->blocks;

    task->blocks = block;
    task->num_blocks++;
    task->total_block_size += input_size;

    *output = b;
    *output_size = input_size;
    return knd_OK;
}

int knd_save_commit_WAL(struct kndTask *task, struct kndCommit *commit)
{
    struct kndTaskContext *ctx = task->ctx;
    struct kndOutput *out = task->out;
    struct kndOutput *file_out = task->file_out;
    size_t planned_journal_size = 0;
    struct stat st;
    int err;

    assert(ctx->repo);
    commit->timestamp = time(NULL);
    if (DEBUG_TASK_LEVEL_TMP) {
        knd_log(".. kndTask #%zu to write a WAL entry: %.*s (path:%.*s)",
                task->id,
                task->input_size, task->input, task->path_size, task->path);
    }

    out->reset(out);
    err = out->writef(out, "{commit %zu{_ts %zu}{_size %zu}",
                      commit->numid, (size_t)commit->timestamp, task->input_size);
    KND_TASK_ERR("commit header output failed");
    err = out->write(out, task->input, task->input_size);
    KND_TASK_ERR("commit body output failed");
    err = out->write(out, "}\n", strlen("}\n"));
    KND_TASK_ERR("commit footer output failed");
    
    file_out->reset(file_out);
    err = file_out->write(file_out, task->path, task->path_size);
    KND_TASK_ERR("system path construction failed");

    if (ctx->repo->path_size) {
        err = out->write(file_out, ctx->repo->path, ctx->repo->path_size);
        KND_TASK_ERR("repo path construction failed");
    }

    err = out->writef(file_out, "agent_%d/", task->id);
    KND_TASK_ERR("agent path construction failed");

    err = knd_mkpath((const char*)file_out->buf, file_out->buf_size, 0755, false);
    KND_TASK_ERR("mkpath %.*s failed", file_out->buf_size, file_out->buf);

    err = file_out->writef(file_out, "journal_%zu.log", ctx->repo->num_journals[task->id]);
    KND_TASK_ERR("log filename construction failed");
    file_out->buf[file_out->buf_size] = '\0';

    knd_log("WAL filename: %.*s", file_out->buf_size, file_out->buf);

    if (stat(file_out->buf, &st)) {
        if (DEBUG_TASK_LEVEL_TMP)
            knd_log(".. initializing the journal: \"%.*s\"",
                    file_out->buf_size, file_out->buf);
        err = knd_write_file((const char*)file_out->buf,
                             "{WAL\n", strlen("{WAL\n"));
        KND_TASK_ERR("failed writing to file %.*s", file_out->buf_size, file_out->buf);
        goto append_wal_rec;
    }

    planned_journal_size = st.st_size + out->buf_size;
    if (planned_journal_size >= ctx->repo->max_journal_size) {
        if (DEBUG_TASK_LEVEL_TMP)
            knd_log("!NB: journal size limit reached!");
        ctx->repo->num_journals[task->id]++;

        file_out->reset(file_out);
        err = out->write(file_out, task->path, task->path_size);
        if (err) return err;

        if (ctx->repo->path_size) {
            err = out->write(file_out, ctx->repo->path, ctx->repo->path_size);
            if (err) return err;
        }
        //err = out->writef(file_out, "journal%zu.log", ctx->repo->num_journals);
        //if (err) return err;
        //file_out->buf[file_out->buf_size] = '\0';

        //if (DEBUG_TASK_LEVEL_TMP)
        //    knd_log(".. switching to a new journal: \"%.*s\"",
        //            file_out->buf_size, file_out->buf);

        // err = knd_write_file((const char*)file_out->buf,
        //                     "{WAL\n", strlen("{WAL\n"));
        // if (err) return err;
    }

 append_wal_rec:

    knd_log(".. append to WAL: %.*s", out->buf_size, out->buf);
    err = knd_append_file((const char*)file_out->buf,
                          out->buf, out->buf_size);
    KND_TASK_ERR("WAL file append failed");

    return knd_OK;
}

int knd_task_context_new(struct kndMemPool *mempool,
                         struct kndTaskContext **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndTaskContext), &page);                RET_ERR();
    *result = page;
    return knd_OK;
}

int knd_task_mem(struct kndMemPool *mempool,
                 struct kndTask **result)
{
    void *page;
    int err;
    err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_BASE,
                                 sizeof(struct kndTask), &page);                   RET_ERR();
    *result = page;
    return knd_OK;
}

int knd_task_block_new(struct kndMemPool *mempool,
                       struct kndTask **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndTask), &page);                   RET_ERR();
    *result = page;
    return knd_OK;
}

int knd_task_new(struct kndShard *shard,
                 struct kndMemPool *mempool,
                 int task_id,
                 struct kndTask **task)
{
    struct kndTask *self;
    struct kndRepo *repo = shard->repo;
    int err;

    self = malloc(sizeof(struct kndTask));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndTask));
    self->shard = shard;
    self->id = task_id;
    self->role = shard->role;

    self->path = shard->path;
    self->path_size = shard->path_size;

    if (!mempool) {
        err = knd_mempool_new(&mempool, 0);
        if (err) return err;
        mempool->type = KND_ALLOC_INCR;
        mempool->num_pages = shard->ctx_mem_config.num_pages;
        mempool->num_small_x4_pages = shard->ctx_mem_config.num_small_x4_pages;
        mempool->num_small_x2_pages = shard->ctx_mem_config.num_small_x2_pages;
        mempool->num_small_pages = shard->ctx_mem_config.num_small_pages;
        mempool->num_tiny_pages = shard->ctx_mem_config.num_tiny_pages;
        err = mempool->alloc(mempool); 
        if (err) return err;
        self->is_mempool_owner = true;
    }
    self->mempool = mempool;

    err = knd_output_new(&self->out, NULL, KND_LARGE_BUF_SIZE);
    if (err) return err;

    err = knd_output_new(&self->log, NULL, KND_TEMP_BUF_SIZE);
    if (err) return err;

    err = knd_output_new(&self->file_out, NULL, KND_FILE_BUF_SIZE);
    if (err) return err;

    /* system repo defaults */
    self->system_repo       = repo;
    self->repo              = repo;

    err = knd_dict_new(&self->class_name_idx, mempool, KND_SMALL_DICT_SIZE);
    if (err) return err;
    err = knd_dict_new(&self->attr_name_idx, mempool, KND_SMALL_DICT_SIZE);
    if (err) return err;
    err = knd_dict_new(&self->proc_name_idx, mempool, KND_SMALL_DICT_SIZE);
    if (err) return err;
    err = knd_dict_new(&self->proc_arg_name_idx, mempool, KND_SMALL_DICT_SIZE);
    if (err) return err;

    *task = self;

    return knd_OK;
}
