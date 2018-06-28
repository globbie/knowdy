#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_task.h"
#include "knd_shard.h"
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

static gsl_err_t parse_user(void *obj,
                            const char *rec,
                            size_t *total_size);

static void del(struct kndTask *self)
{
    self->log->del(self->log);
    self->spec_out->del(self->spec_out);
    self->update->del(self->update);
    free(self);
}

static void str(struct kndTask *self __attribute__((unused)), size_t depth __attribute__((unused)))
{

}

static void reset(struct kndTask *self)
{
    self->sid_size = 0;
    self->uid_size = 0;
    self->tid_size = 0;

    self->locale = self->shard->user->default_locale;
    self->locale_size = self->shard->user->default_locale_size;
    self->curr_locale_size = 0;

    self->delivery_type = KND_DELIVERY_CACHE;
    self->delivery_addr_size = 0;

    memset(self->state, '0', KND_STATE_SIZE);
    self->is_state_changed = false;

    self->type = KND_GET_STATE;

    self->num_sets = 0;

    self->batch_max = KND_RESULT_BATCH_SIZE;
    self->batch_size = 0;
    self->batch_from = 0;
    self->start_from = 0;
    self->match_count = 0;

    self->error = 0;
    self->http_code = HTTP_OK;

    self->log->reset(self->log);
    self->out->reset(self->out);
    self->update->reset(self->update);
    self->spec_out->reset(self->spec_out);
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
          .parse = parse_user,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_user(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndTask *self = obj;
    struct kndUser *user = self->shard->user;

    if (self->curr_locale_size) {
        self->locale = self->curr_locale;
        self->locale_size = self->curr_locale_size;
    }

    return user->parse_task(user, rec, total_size);
}

static gsl_err_t set_output_format(void *obj, const char *name, size_t name_size)
{
    struct kndTask *self = obj;
    const char *format_str;
    size_t format_str_size;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    for (size_t i = 0; i < sizeof(knd_format_names); i++) {
        format_str = knd_format_names[i];
        if (!format_str) break;

        format_str_size = strlen(format_str);
        if (name_size != format_str_size) continue;

        if (!memcmp(knd_format_names[i], name, name_size)) {
            self->format = (knd_format)i;
            return make_gsl_err(gsl_OK);
        }
    }

    err = self->log->write(self->log, name, name_size);
    if (err) return make_gsl_err(err);
    err = self->log->write(self->log, " format not supported", strlen(" format not supported"));
    if (err) return make_gsl_err(err);

    return make_gsl_err(gsl_FAIL);
}


static gsl_err_t check_delivery_type(void *obj, const char *val, size_t val_size)
{
    const char *schema_name = "HTTP";
    size_t schema_name_size = strlen(schema_name);
    struct kndTask *self = obj;

    if (val_size != schema_name_size)  return make_gsl_err(gsl_FAIL);
    if (memcmp(schema_name, val, val_size)) return make_gsl_err(gsl_FAIL);

    self->delivery_type = KND_DELIVERY_HTTP;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_delivery_callback(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *self = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_delivery_type,
          .obj = self
        },
        { .name = "base_url",
          .name_size = strlen("base_url"),
          .buf = self->delivery_addr,
          .buf_size = &self->delivery_addr_size,
          .max_buf_size = KND_NAME_SIZE
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_task(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndTask *self = obj;
    gsl_err_t err;

    reset(self);

    struct gslTaskSpec specs[] = {
        { .name = "schema",
          .name_size = strlen("schema"),
          .buf = self->schema_name,
          .buf_size = &self->schema_name_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .name = "tid",
          .name_size = strlen("tid"),
          .buf = self->tid,
          .buf_size = &self->tid_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .name = "locale",
          .name_size = strlen("locale"),
          .buf = self->curr_locale,
          .buf_size = &self->curr_locale_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .name = "format",
          .name_size = strlen("format"),
          .run = set_output_format,
          .obj = self
        }/*,
        { .name = "callback",
          .name_size = strlen("callback"),
          .parse = parse_delivery_callback,
          .obj = self
          }*/,
        { .name = "user",
          .name_size = strlen("user"),
          .parse = parse_user,
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

    knd_log("++ task completed!");

    /* check mandatory fields */
    /*if (!self->tid_size) {
        switch (self->type) {
        case KND_UPDATE_STATE:
        case KND_LIQUID_STATE:
            return make_gsl_err(gsl_OK);
        default:
            knd_log("-- no TID found");
            return make_gsl_err_external(knd_FAIL);
        }
    }
    */
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
  int err;

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

  char buf[256];
  err = snprintf(buf, sizeof buf,  "parser error at line %zu:%zu: %d %s",
                 line + 1, column + 1, parser_err.code, gsl_err_to_str(parser_err));
  if (err < 0) return knd_IO_FAIL;

  return self->log->write(self->log, buf, strlen(buf));
}

static int parse_GSL(struct kndTask *self,
                     const char *rec,
                     size_t rec_size,
                     const char *obj,
                     size_t obj_size)
{
    if (DEBUG_TASK_LEVEL_2)
        knd_log(".. parsing task: \"%.*s\"..", 256, rec);

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
    self->obj = obj;
    self->obj_size = obj_size;

    parser_err = gsl_parse_task(rec, &total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- task failure :(");
        if (!is_gsl_err_external(parser_err)) {
            assert(!self->log->buf_size);
            err = log_parser_error(self, parser_err, total_size, rec);
            if (err) return err;
        }

        if (!self->log->buf_size) {
            err = self->log->write(self->log, "internal server error",
                                 strlen("internal server error"));
            if (err) return err;
        }

        return gsl_err_to_knd_err_codes(parser_err);
    }

    return knd_OK;
}


static int build_report(struct kndTask *self)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct glbOutput *out = self->spec_out;
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

        err = out->write(out, "{err ", strlen("{err "));
        if (err) return err;
        err = out->write(out, self->log->buf, self->log->buf_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;

        /* TODO: build JSON reply in loco */
        self->out->reset(self->out);
        err = self->out->write(self->out, "{\"err\":\"", strlen("{\"err\":\""));
        if (err) return err;
        err = self->out->write(self->out, self->log->buf, self->log->buf_size);
        if (err) return err;
        err = self->out->write(self->out, "\"", strlen("\""));
        if (err) return err;

        if (self->http_code != HTTP_OK) {
            err = self->out->write(self->out,
                                   ",\"http_code\":", strlen(",\"http_code\":"));
            if (err) return err;
            buf_size = sprintf(buf, "%d", self->http_code);
            err = self->out->write(self->out, buf, buf_size);
            if (err) return err;
        }

        err = self->out->write(self->out, "}", strlen("}"));
        if (err) return err;

        self->report = self->out->buf;
        self->report_size = self->out->buf_size;
        return knd_OK;
    }

    if (!self->out->buf_size) {
        err = self->out->write(self->out,
                               "{\"update\":\"OK\"}",
                               strlen("{\"update\":\"OK\"}"));
        if (err) return err;
    }

    if (DEBUG_TASK_LEVEL_TMP) {
        obj_size = self->out->buf_size;
        if (obj_size > KND_MAX_DEBUG_CONTEXT_SIZE)
            obj_size = KND_MAX_DEBUG_CONTEXT_SIZE;

        knd_log("RESULT: \"%s\" %.*s [size: %zu]\n",
                task_status, obj_size,
                self->out->buf, self->out->buf_size);
    }

    /* report body */
    self->report = self->out->buf;
    self->report_size = self->out->buf_size;

    /* send delta */
    if (self->type == KND_DELTA_STATE) {
        if (self->update->buf_size) {
            self->report = self->update->buf;
            self->report_size = self->update->buf_size;
        }
    }

    /* TODO: inform all retrievers about the state change */
    if (self->type == KND_UPDATE_STATE) {
        /*err = self->out->write(self->out,
                               "{\"update\":\"OK\"}",
                               strlen("{\"update\":\"OK\"}"));
        if (err) return err;
        */
        self->report = self->out->buf;
        self->report_size = self->out->buf_size;
        
        if (DEBUG_TASK_LEVEL_2) {
            chunk_size =  self->update->buf_size > KND_MAX_DEBUG_CHUNK_SIZE ?\
                KND_MAX_DEBUG_CHUNK_SIZE :  self->update->buf_size;
            knd_log("\n\n** UPDATE retrievers: \"%.*s\" [%zu]",
                    chunk_size, self->report,
                    self->report_size);
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

    err = glbOutput_new(&self->update, KND_LARGE_BUF_SIZE);
    if (err) return err;

    err = glbOutput_new(&self->file_out, KND_FILE_BUF_SIZE);
    if (err) return err;

    self->visual.text_line_height = KND_TEXT_LINE_HEIGHT;
    self->visual.text_hangindent_size = KND_TEXT_HANGINDENT_SIZE;

    self->del    = del;
    self->str    = str;
    self->reset  = reset;
    self->run    = parse_GSL;
    self->build_report = build_report;

    *task = self;

    return knd_OK;
}
