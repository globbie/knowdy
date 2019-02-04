#include "knd_task.h"

#include "knd_class.h"
#include "knd_proc.h"
#include "knd_repo.h"
#include "knd_shard.h"
#include "knd_user.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#include <assert.h>
#include <string.h>
#include <stdatomic.h>

#define DEBUG_TASK_LEVEL_0 0
#define DEBUG_TASK_LEVEL_1 0
#define DEBUG_TASK_LEVEL_2 0
#define DEBUG_TASK_LEVEL_3 0
#define DEBUG_TASK_LEVEL_TMP 1

#if 0
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
#endif

#if 0
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
#endif

static gsl_err_t run_set_format(void *obj,
                                const char *name,
                                size_t name_size)
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


static gsl_err_t parse_format(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndTask *self = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_format,
          .obj = self
        },
        { .name = "offset",
          .name_size = strlen("offset"),
          .parse = gsl_parse_size_t,
          .obj = &self->format_offset
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

/*static gsl_err_t parse_user(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndTask *self = obj;

    if (self->curr_locale_size) {
        self->locale = self->curr_locale;
        self->locale_size = self->curr_locale_size;
    }
    return knd_parse_select_user(self, rec, total_size);
}
*/

static gsl_err_t parse_class_import(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *task = obj;
    int err;

    if (DEBUG_TASK_LEVEL_TMP)
        knd_log(".. parsing the system class import: \"%.*s\"..", 64, rec);

    task->type = KND_UPDATE_STATE;
    if (!task->ctx->update) {
        err = knd_update_new(task->mempool, &task->ctx->update);
        if (err) return make_gsl_err_external(err);

        err = knd_dict_new(&task->ctx->class_name_idx, KND_SMALL_DICT_SIZE);
        if (err) return make_gsl_err_external(err);

        task->ctx->update->orig_state_id = atomic_load_explicit(&task->repo->num_updates,
                                                                memory_order_relaxed);
    }

    return knd_class_import(task->repo, rec, total_size, task);
}

static gsl_err_t parse_class_select(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *task = obj;

    if (DEBUG_TASK_LEVEL_2)
        knd_log(".. parsing the system class select: \"%.*s\"", 64, rec);

    return knd_class_select(task->repo, rec, total_size, task);
}

static gsl_err_t parse_proc_import(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndRepo *repo = task->repo;
    int err;

    if (DEBUG_TASK_LEVEL_2)
        knd_log(".. parsing the system proc import: \"%.*s\"..", 64, rec);

    task->type = KND_UPDATE_STATE;
    if (!task->ctx->update) {
        err = knd_update_new(task->mempool, &task->ctx->update);
        if (err) return make_gsl_err_external(err);

        err = knd_dict_new(&task->ctx->proc_name_idx, KND_SMALL_DICT_SIZE);
        if (err) return make_gsl_err_external(err);

        task->ctx->update->orig_state_id = atomic_load_explicit(&repo->num_updates,
                                                                memory_order_relaxed);
    }

    return knd_proc_import(task->repo, rec, total_size, task);
}

static gsl_err_t parse_proc_select(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *task = obj;

    if (DEBUG_TASK_LEVEL_2)
        knd_log(".. parsing the system proc select: \"%.*s\"", 64, rec);

    return knd_proc_select(task->repo, rec, total_size, task);
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
          .parse = knd_parse_select_user,
          .obj = self
        }
    };

    self->type = KND_LIQUID_STATE;
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_task(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *self = obj;
    gsl_err_t parser_err;
    int err;

    struct gslTaskSpec specs[] = {
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
          .parse = parse_format,
          .obj = self
        },
        { .name = "user",
          .name_size = strlen("user"),
          .parse = knd_parse_select_user,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_import,
          .obj = self
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_import,
          .obj = self
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_select,
          .obj = self
        },
        { .name = "repo",
          .name_size = strlen("repo"),
          .parse = knd_parse_repo,
          .obj = self
        },
        { .name = "update",
          .name_size = strlen("update"),
          .parse = parse_update,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- task parse failure: \"%.*s\"",
                self->log->buf_size, self->log->buf);
        goto cleanup;
    }

    /* any system repo updates? */
    switch (self->type) {
    case KND_UPDATE_STATE:
        if (!self->ctx->update_confirmed) {
            err = knd_confirm_updates(self->repo, self);
            if (err) return make_gsl_err_external(err);
        }
        break;
    default:
        break;
    }

    return make_gsl_err(gsl_OK);

 cleanup:
    // any resources to free?
    return parser_err;
}

int knd_task_run(struct kndTask *self)
{
    size_t total_size = self->ctx->input_size;
    gsl_err_t parser_err;

    if (DEBUG_TASK_LEVEL_TMP) {
        size_t chunk_size = KND_TEXT_CHUNK_SIZE;
        if (self->ctx->input_size < chunk_size)
            chunk_size = self->ctx->input_size;
        knd_log("== input: %.*s ..", chunk_size, self->ctx->input);
    }

    struct gslTaskSpec specs[] = {
        { .name = "task",
          .name_size = strlen("task"),
          .parse = parse_task,
          .obj = self
        }
    };
    parser_err = gsl_parse_task(self->ctx->input, &total_size,
                                specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- task run failure");
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
