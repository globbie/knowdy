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

static void del(struct kndTask *self)
{
    self->log->del(self->log);
    self->spec_out->del(self->spec_out);
    self->update_out->del(self->update_out);
    free(self);
}

static void reset(struct kndTask *self)
{
    self->tid_size = 0;

    self->locale = self->shard->user->default_locale;
    self->locale_size = self->shard->user->default_locale_size;
    self->curr_locale_size = 0;

    self->type = KND_GET_STATE;
    self->phase = KND_SELECTED;

    self->num_sets = 0;
    
    self->batch_max = KND_RESULT_BATCH_SIZE;
    self->batch_size = 0;
    self->batch_from = 0;
    self->start_from = 0;
    self->match_count = 0;

    self->batch_eq = 0;
    self->batch_gt = 0;
    self->batch_lt = 0;

    /* initialize request with off limit values */
    self->state_eq = -1;
    self->state_gt = -1;
    self->state_gte = -1;
    self->state_lt = 0;
    self->state_lte = 0;
    self->show_removed_objs = false;
    self->show_rels = false;
    self->max_depth = 1;

    self->error = 0;
    self->http_code = HTTP_OK;
    self->update = NULL;

    self->curr_inst = NULL;
    self->log->reset(self->log);
    self->out->reset(self->out);
    self->update_out->reset(self->update_out);
    self->spec_out->reset(self->spec_out);
}

static gsl_err_t select_user(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndTask *self = obj;
    struct kndUser *user = self->shard->user;

    if (self->curr_locale_size) {
        self->locale = self->curr_locale;
        self->locale_size = self->curr_locale_size;
    }
    return user->select(user, rec, total_size);
}

static gsl_err_t open_system_repo(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndTask *self = obj;

    knd_log(".. opening system repo..");

    struct gslTaskSpec specs[] = {
        { .name = "user",
          .name_size = strlen("user"),
          .parse = select_user,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_output_format(void *obj, const char *name, size_t name_size)
{
    struct kndTask *self = obj;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    for (size_t i = 0; i < sizeof knd_format_names / sizeof knd_format_names[0]; i++) {
        const char *format_str = knd_format_names[i];
        assert(format_str != NULL);

        size_t format_str_size = strlen(format_str);
        if (name_size != format_str_size) continue;

        if (!memcmp(format_str, name, name_size)) {
            self->format = (knd_format)i;
            return make_gsl_err(gsl_OK);
        }
    }

    err = self->log->write(self->log, name, name_size);
    if (err) return make_gsl_err_external(err);
    err = self->log->write(self->log, " format not supported",
                           strlen(" format not supported"));
    if (err) return make_gsl_err_external(err);

    return make_gsl_err_external(knd_NO_MATCH);
}

static gsl_err_t parse_update(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndTask *self = obj;

    self->type = KND_LIQUID_STATE;

    struct gslTaskSpec specs[] = {
        { .name = "_ts",
          .name_size = strlen("_ts"),
          .buf = self->timestamp,
          .buf_size = &self->timestamp_size,
          .max_buf_size = sizeof self->timestamp
        },
        { .name = "user",
          .name_size = strlen("user"),
          .parse = select_user,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_class_import(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *self = obj;
    struct kndClass *c = self->shard->repo->root_class;

    if (DEBUG_TASK_LEVEL_TMP)
        knd_log(".. parsing the system class import: \"%.*s\"..", 64, rec);
    self->type = KND_UPDATE_STATE;
    return knd_class_import(c, rec, total_size);
}

static gsl_err_t parse_class_select(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *self = obj;
    struct kndClass *c = self->shard->repo->root_class;

    if (DEBUG_TASK_LEVEL_TMP)
        knd_log(".. parsing the system class select: \"%.*s\"", 64, rec);

    return knd_class_select(c, rec, total_size);
}

static gsl_err_t parse_task(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *self = obj;
    gsl_err_t err;

    struct gslTaskSpec specs[] = {
        /*{ .name = "schema",
          .name_size = strlen("schema"),
          .buf = self->schema_name,
          .buf_size = &self->schema_name_size,
          .max_buf_size = sizeof self->schema_name
          },*/
        { .name = "tid",
          .name_size = strlen("tid"),
          .buf = self->tid,
          .buf_size = &self->tid_size,
          .max_buf_size = sizeof self->tid
        },
        { .name = "locale",
          .name_size = strlen("locale"),
          .buf = self->curr_locale,
          .buf_size = &self->curr_locale_size,
          .max_buf_size = sizeof self->curr_locale
        },
        { .name = "format",
          .name_size = strlen("format"),
          .run = set_output_format,
          .obj = self
        },
        { .name = "user",
          .name_size = strlen("user"),
          .parse = select_user,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_import,
          .obj = self
        }/*,
        { .type = GSL_SET_ARRAY_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_array,
          .obj = self
          }*/,
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = self
        },
        { .name = "repo",
          .name_size = strlen("repo"),
          .parse = open_system_repo,
          .obj = self
        },
        { .name = "update",
          .name_size = strlen("update"),
          .parse = parse_update,
          .obj = self
        }
    };

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
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

static int parse_GSL(struct kndTask *self, const char *rec, size_t rec_size, const char *obj, size_t obj_size)
{
    if (DEBUG_TASK_LEVEL_TMP) knd_log(".. parsing task: \"%.*s\"..", 256, rec);

    reset(self);

    struct gslTaskSpec specs[] = {
        { .name = "task",
          .name_size = strlen("task"),
          .parse = parse_task,
          .obj = self
        }
    };
    size_t total_size = rec_size;
    int err;
    gsl_err_t parser_err;

    self->spec = rec;
    self->spec_size = rec_size;
    self->obj = obj;  // FIXME(k15tfu): obj & obj_size are not used
    self->obj_size = obj_size;

    parser_err = gsl_parse_task(rec, &total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- task failure :(");
        if (!is_gsl_err_external(parser_err)) {
            // assert(!self->log->buf_size)
            if (!self->log->buf_size) {
                err = log_parser_error(self, parser_err, total_size, rec);
                if (err) return err;
            }
        }

        if (!self->log->buf_size) {
            err = self->log->write(self->log, "internal server error", strlen("internal server error"));
            if (err) return err;
        }

        return gsl_err_to_knd_err_codes(parser_err);
    }

    return knd_OK;
}

static int build_report(struct kndTask *self)
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

extern int kndTask_new(struct kndTask **task)
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

    err = glbOutput_new(&self->spec_out, KND_MED_BUF_SIZE);
    if (err) return err;

    err = glbOutput_new(&self->update_out, KND_LARGE_BUF_SIZE);
    if (err) return err;

    err = glbOutput_new(&self->file_out, KND_FILE_BUF_SIZE);
    if (err) return err;

    self->del    = del;
    self->reset  = reset;
    self->run    = parse_GSL;
    self->build_report = build_report;

    *task = self;

    return knd_OK;
}
