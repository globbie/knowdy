#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_task.h"
#include "knd_shard.h"
#include "knd_repo.h"
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
    self->update_out->del(self->update_out);
    self->file_out->del(self->file_out);
    self->task_out->del(self->task_out);
    self->mempool->del(self->mempool);
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

    self->user_ctx = NULL;
    self->repo = self->system_repo;

    self->log->reset(self->log);
    self->update_out->reset(self->update_out);
}

static int task_err_export_JSON(struct kndTaskContext *self,
                                struct kndOutput *out)
{
    int err;

    err = out->write(out, "{\"err\":\"", strlen("{\"err\":\""));
    if (err) return err;

    if (self->log->buf_size) {
        err = out->write(out, self->log->buf, self->log->buf_size);
        if (err) return err;
    } else {
        self->http_code = HTTP_INTERNAL_SERVER_ERROR;
        err = out->write(out, "internal server error", strlen("internal server error"));
        if (err) return err;
    }
    err = out->write(out, "\"", strlen("\""));
    if (err) return err;

    if (self->http_code != HTTP_OK) {
        err = out->write(out, ",\"http_code\":", strlen(",\"http_code\":"));
        if (err) return err;
        err = out->writef(out, "%d", self->http_code);
        if (err) return err;
    } else {
        self->http_code = HTTP_NOT_FOUND;
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

static int task_err_export_GSP(struct kndTaskContext *self,
                               struct kndOutput *out)
{
    int err;
    err = out->write(out, "{err ", strlen("{err "));                              RET_ERR();
    err = out->writef(out, "%d", self->http_code);                                RET_ERR();

    err = out->write(out, "{gloss ", strlen("{gloss "));                          RET_ERR();
    if (self->log->buf_size) {
        err = out->write(out, self->log->buf, self->log->buf_size);               RET_ERR();
    } else {
        err = out->write(out, "internal error", strlen("internal error"));        RET_ERR();
    }
    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

int knd_task_err_export(struct kndTaskContext *self)
{
    struct kndOutput *out = self->out;
    knd_format format = self->format;
    int err;

    out->reset(out);

    switch (format) {
    case KND_FORMAT_JSON:
        err = task_err_export_JSON(self, out);                                    RET_ERR();
        break;
    default:
        err = task_err_export_GSP(self, out);                                     RET_ERR();
        break;
    }
    return knd_OK;
}

int knd_task_run(struct kndTask *self)
{
    size_t total_size = self->ctx->input_size;
    gsl_err_t parser_err;

    if (DEBUG_TASK_LEVEL_2) {
        size_t chunk_size = KND_TEXT_CHUNK_SIZE;
        if (self->ctx->input_size < chunk_size)
            chunk_size = self->ctx->input_size;
        knd_log("== INPUT (size:%zu): %.*s ..",
                self->ctx->input_size,
                chunk_size, self->ctx->input);
    }

    struct gslTaskSpec specs[] = {
        { .name = "task",
          .name_size = strlen("task"),
          .parse = knd_parse_task,
          .obj = self
        }
    };
    parser_err = gsl_parse_task(self->ctx->input, &total_size,
                                specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        /*if (!is_gsl_err_external(parser_err)) {
            if (!self->log->buf_size) {
                self->http_code = HTTP_BAD_REQUEST;
                err = log_parser_error(self, parser_err, self->input_size, self->input);
                if (err) return err;
            }
        }
        if (!self->log->buf_size) {
            self->http_code = HTTP_INTERNAL_SERVER_ERROR;
            err = self->log->writef(self->log, "unclassified server error");
            if (err) return err;
            }*/
        return gsl_err_to_knd_err_codes(parser_err);
    }
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

int knd_task_new(struct kndShard *shard,
                 struct kndMemPool *mempool,
                 struct kndTask **task)
{
    struct kndTask *self;
    int err;

    self = malloc(sizeof(struct kndTask));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndTask));
    self->shard = shard;

    if (!mempool) {
        err = kndMemPool_new(&mempool);
        if (err) return err;
        mempool->type = KND_ALLOC_INCR;
        mempool->num_pages = shard->ctx_mem_config.num_pages;
        mempool->num_small_x4_pages = shard->ctx_mem_config.num_small_x4_pages;
        mempool->num_small_x2_pages = shard->ctx_mem_config.num_small_x2_pages;
        mempool->num_small_pages = shard->ctx_mem_config.num_small_pages;
        mempool->num_tiny_pages = shard->ctx_mem_config.num_tiny_pages;
        err = mempool->alloc(mempool); 
        if (err) return err;
    }
    self->mempool = mempool;
    
    err = knd_output_new(&self->log, NULL, KND_TEMP_BUF_SIZE);
    if (err) return err;

    err = knd_output_new(&self->update_out, NULL, KND_LARGE_BUF_SIZE);
    if (err) return err;

    err = knd_output_new(&self->file_out, NULL, KND_FILE_BUF_SIZE);
    if (err) return err;

    err = knd_output_new(&self->task_out, NULL, KND_TASK_STORAGE_SIZE);
    if (err) return err;

    *task = self;

    return knd_OK;
}
