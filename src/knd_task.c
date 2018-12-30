#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_task.h"
#include "knd_shard.h"
#include "knd_repo.h"
#include "knd_user.h"
#include "knd_mempool.h"
#include "knd_utils.h"
#include "knd_class.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>
#include <gsl-parser/gsl_err.h>

#define DEBUG_TASK_LEVEL_0 0
#define DEBUG_TASK_LEVEL_1 0
#define DEBUG_TASK_LEVEL_2 0
#define DEBUG_TASK_LEVEL_3 0
#define DEBUG_TASK_LEVEL_TMP 1

void kndTask_del(struct kndTask *self)
{
    self->log->del(self->log);
    self->out->del(self->out);
    self->update_out->del(self->update_out);
    self->file_out->del(self->file_out);
    self->storage->del(self->storage);
    self->mempool->del(self->mempool);
    free(self);
}

void kndTask_reset(struct kndTask *self)
{
    self->tid_size = 0;

    self->locale = self->shard->user->default_locale;
    self->locale_size = self->shard->user->default_locale_size;
    self->curr_locale_size = 0;

    self->type = KND_GET_STATE;
    self->phase = KND_SELECTED;

    self->format = KND_FORMAT_GSL;
    self->format_offset = 0;

    self->num_sets = 0;
    
    self->batch_max = KND_RESULT_BATCH_SIZE;
    self->batch_size = 0;
    self->batch_from = 0;
    self->start_from = 0;

    /* initialize request with off limit values */
    self->state_eq = -1;
    self->state_gt = -1;
    self->state_gte = -1;
    self->state_lt = 0;
    self->state_lte = 0;
    self->show_removed_objs = false;
    self->depth = 0;
    self->max_depth = 1;

    self->error = 0;
    self->http_code = HTTP_OK;
    self->update = NULL;
    self->update_confirmed = false;

    self->user_ctx = NULL;
    self->repo = NULL;
    self->class = NULL;
    self->class_inst = NULL;
    self->attr = NULL;
    self->attr_var = NULL;

    self->log->reset(self->log);
    self->out->reset(self->out);
    self->update_out->reset(self->update_out);
}

static const char * gsl_err_to_str(gsl_err_t err)
{
    switch (err.code) {
    case gsl_FAIL:     return "Unclassified error";
    case gsl_LIMIT:    return "LIMIT error";
    case gsl_NO_MATCH: return "NO_MATCH error";
    case gsl_FORMAT:   return "FORMAT error";
    case gsl_EXISTS:   return "EXISTS error";
    default:           return "Unknown error";
    }
}

static int log_parser_error(struct kndTask *self,
                           gsl_err_t parser_err,
                           size_t pos,
                           const char *rec)
{
    size_t line = 0, column;
    for (;;) {
        const char *next_line = strchr(rec, '\n');
        if (next_line == NULL) break;

        size_t len = next_line + 1 - rec;
        if (len > pos) break;

        line++;
        rec = next_line + 1;
        pos -= len;
    }
    column = pos;

    return self->log->writef(self->log, "parser error at line %zu:%zu: %d %s",
                             line + 1, column + 1, parser_err.code, gsl_err_to_str(parser_err));
}

int kndTask_run(struct kndTask *self, const char *rec, size_t rec_size, struct kndShard *shard)
{
    int err;
    gsl_err_t parser_err;

    kndTask_reset(self);

    self->input = rec;
    self->input_size = rec_size;

    parser_err = knd_select_task(self, rec, &rec_size, shard);
    if (parser_err.code) {
        knd_log("-- task run failure");
        if (!is_gsl_err_external(parser_err)) {
            // assert(!self->log->buf_size)
            if (!self->log->buf_size) {
                err = log_parser_error(self, parser_err, rec_size, rec);
                if (err) return err;
            }
        }
        if (!self->log->buf_size) {
            err = self->log->writef(self->log, "unclassified server error");
            if (err) return err;
        }
        return gsl_err_to_knd_err_codes(parser_err);
    }
    return knd_OK;
}

int kndTask_build_report(struct kndTask *self)
{
    size_t obj_size;
    size_t chunk_size;
    const char *task_status = "++";
    int err;

    if (DEBUG_TASK_LEVEL_2)
        knd_log("..TASK (type: %d) reporting..", self->type);

    self->report = NULL;
    self->report_size = 0;

    if (self->error) {
        switch (self->error) {
        case knd_NOMEM:
        case knd_IO_FAIL:
            self->http_code = HTTP_INTERNAL_SERVER_ERROR;
            break;
        default:
            break;
        }

        /* TODO: build JSON reply in loco */
        self->out->reset(self->out);

        err = self->out->write(self->out, "{\"err\":\"", strlen("{\"err\":\""));
        if (err) return err;

        if (self->log->buf_size) {
            err = self->out->write(self->out, self->log->buf, self->log->buf_size);
            if (err) return err;
        } else {
            err = self->out->write(self->out, "internal error", strlen("internal error"));
            if (err) return err;
        }

        err = self->out->write(self->out, "\"", strlen("\""));
        if (err) return err;

        if (self->http_code != HTTP_OK) {
            err = self->out->write(self->out, ",\"http_code\":", strlen(",\"http_code\":"));
            if (err) return err;
            err = self->out->writef(self->out, "%d", self->http_code);
            if (err) return err;
        } else {
            self->http_code = HTTP_NOT_FOUND;
            // convert error code to HTTP error
            err = self->out->write(self->out, ",\"http_code\":", strlen(",\"http_code\":"));
            if (err) return err;
            err = self->out->writef(self->out, "%d", HTTP_NOT_FOUND);
            if (err) return err;
        }
        err = self->out->write(self->out, "}", strlen("}"));
        if (err) return err;

        self->report = self->out->buf;
        self->report_size = self->out->buf_size;
        return knd_OK;
    }

    if (!self->out->buf_size) {
        err = self->out->write(self->out, "{\"result\":\"OK\"}", strlen("{\"result\":\"OK\"}"));
        if (err) return err;
    }

    if (DEBUG_TASK_LEVEL_TMP) {
        obj_size = self->out->buf_size;
        if (obj_size > KND_MAX_DEBUG_CONTEXT_SIZE) obj_size = KND_MAX_DEBUG_CONTEXT_SIZE;
        knd_log("== RESULT: \"%s\" %.*s [size: %zu]\n", task_status, (int) obj_size, self->out->buf, self->out->buf_size);
    }

    /* report body */
    self->report = self->out->buf;
    self->report_size = self->out->buf_size;

    /* send delta */
    if (self->type == KND_DELTA_STATE) {
        if (self->update_out->buf_size) {
            self->report = self->update_out->buf;
            self->report_size = self->update_out->buf_size;
        }
    }

    if (self->type == KND_UPDATE_STATE) {
        self->report = self->out->buf;
        self->report_size = self->out->buf_size;
        if (DEBUG_TASK_LEVEL_2) {
            chunk_size =  self->update_out->buf_size > KND_MAX_DEBUG_CHUNK_SIZE ? KND_MAX_DEBUG_CHUNK_SIZE :  self->update_out->buf_size;
            knd_log("\n\n** UPDATE retrievers: \"%.*s\" [%zu]", chunk_size, self->report, self->report_size);
        }
    }
    return knd_OK;
}

int kndTask_new(struct kndTask **task)
{
    struct kndTask *self;
    int err;

    self = malloc(sizeof(struct kndTask));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndTask));

    err = glbOutput_new(&self->log, KND_TEMP_BUF_SIZE);
    if (err) return err;

    err = glbOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err) return err;

    err = glbOutput_new(&self->update_out, KND_LARGE_BUF_SIZE);
    if (err) return err;

    err = glbOutput_new(&self->file_out, KND_FILE_BUF_SIZE);
    if (err) return err;

    err = glbOutput_new(&self->storage, KND_TASK_STORAGE_SIZE);
    if (err) return err;

    *task = self;

    return knd_OK;
}
