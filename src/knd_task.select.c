#include "knd_task.h"
#include "knd_repo.h"
#include "knd_class.h"
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
    if (!task->repo)
        task->repo = task->root_repo;
    struct kndClass *c = task->repo->root_class;
    int err;

    if (DEBUG_TASK_LEVEL_TMP)
        knd_log(".. parsing the system class import: \"%.*s\"..", 64, rec);

    task->type = KND_UPDATE_STATE;
    err = knd_class_import(c, rec, total_size, task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    return *total_size = 0, make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_select(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *task = obj;
    if (!task->repo)
        task->repo = task->root_repo;

    struct kndClass *c = task->repo->root_class;

    if (DEBUG_TASK_LEVEL_TMP)
        knd_log(".. parsing the system class select: \"%.*s\"", 64, rec);

    task->root_class = c;
    task->class = c;

    return knd_select_class(task->repo, rec, total_size, task);
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
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

gsl_err_t knd_select_task(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *self = obj;
    struct gslTaskSpec specs[] = {
        { .name = "task",
          .name_size = strlen("task"),
          .parse = parse_task,
          .obj = self
        }
    };

    if (DEBUG_TASK_LEVEL_TMP)
        knd_log(".. parsing task: \"%.*s\"..", 256, rec);

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
