#include "knd_task.h"

#include "knd_class.h"
#include "knd_repo.h"
#include "knd_shard.h"
#include "knd_user.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#include <assert.h>
#include <string.h>

#define DEBUG_TASK_LEVEL_0 0
#define DEBUG_TASK_LEVEL_1 0
#define DEBUG_TASK_LEVEL_2 0
#define DEBUG_TASK_LEVEL_3 0
#define DEBUG_TASK_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *self;
    struct kndShard *shard;
};

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
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->self;
    if (!task->repo)
        task->repo = ctx->shard->repo;

    if (DEBUG_TASK_LEVEL_TMP)
        knd_log(".. parsing the system class import: \"%.*s\"..", 64, rec);

    task->type = KND_UPDATE_STATE;
    return knd_class_import(ctx->shard->repo, rec, total_size, task);
}

static gsl_err_t parse_class_select(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->self;
    if (!task->repo)
        task->repo = ctx->shard->repo;

    struct kndClass *c = task->repo->root_class;

    if (DEBUG_TASK_LEVEL_TMP)
        knd_log(".. parsing the system class select: \"%.*s\"", 64, rec);

    task->class = c;

    return knd_class_select(task->repo, rec, total_size, task);
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
    struct LocalContext *ctx = obj;
    struct kndTask *self = ctx->self;
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
          .obj = ctx
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = ctx
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

    switch (self->type) {
    case KND_UPDATE_STATE:
        if (!self->update_confirmed) {
            err = knd_confirm_state(self->repo, self);
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

gsl_err_t knd_select_task(struct kndTask *self, const char *rec, size_t *total_size, struct kndShard *shard)
{
    struct LocalContext ctx = { self, shard };
    struct gslTaskSpec specs[] = {
        { .name = "task",
          .name_size = strlen("task"),
          .parse = parse_task,
          .obj = &ctx
        }
    };
    if (DEBUG_TASK_LEVEL_TMP)
        knd_log(".. parsing task: \"%.*s\"..", 256, rec);

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
