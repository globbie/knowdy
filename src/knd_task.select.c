#include "knd_task.h"

#include "knd_class.h"
#include "knd_user.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#include <assert.h>
#include <string.h>

#include "knd_repo.h"   // FIXME(k15tfu): ?? remove this
#include "knd_shard.h"  // FIXME(k15tfu): ?? remove this

#define DEBUG_TASK_LEVEL_0 0
#define DEBUG_TASK_LEVEL_1 0
#define DEBUG_TASK_LEVEL_2 0
#define DEBUG_TASK_LEVEL_3 0
#define DEBUG_TASK_LEVEL_TMP 1

static gsl_err_t present_repo_state(void *obj,
                                    const char *unused_var(name),
                                    size_t unused_var(name_size))
{
    struct kndTask *task = obj;
    //struct kndRepo *repo = task->repo;
    struct glbOutput *out = task->out;
    //struct kndMemPool *mempool = task->mempool;
    //struct kndSet *set;
    //struct kndState *latest_state;
    int err;

    task->type = KND_SELECT_STATE;

    //if (!repo->states)                                     goto show_curr_state;

    //latest_state = self->states;

    //if (task->state_gt >= latest_state->numid)             goto show_curr_state;
    //if (task->state_lt && task->state_lt < task->state_gt) goto show_curr_state;

    if (DEBUG_TASK_LEVEL_TMP) {
        knd_log(".. select repo delta:  gt %zu  lt %zu  eq:%zu..",
                task->state_gt, task->state_lt, task->state_eq);
    }

    /*    err = knd_set_new(mempool, &set);
    if (err) return make_gsl_err_external(err);
    set->mempool = mempool;

    err = knd_class_get_updates(self,
                                task->state_gt, task->state_lt,
                                task->state_eq, set);
    if (err) return make_gsl_err_external(err);
    task->show_removed_objs = true;

    err =  knd_class_export_set_JSON(set, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
    */
    //show_curr_state:

    err = out->writec(out, '{');
    if (err) return make_gsl_err_external(err);

    //err = knd_export_class_state_JSON(self, task);
    //if (err) return make_gsl_err_external(err);

    err = out->writec(out, '}');
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_repo_state(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndTask *task = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = knd_set_curr_state,
          .obj = task
        },
        { .name = "gt",
          .name_size = strlen("gt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gt
        },
        { .name = "gte",
          .name_size = strlen("gte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gte
        },
        { .name = "lt",
          .name_size = strlen("lt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lt
        },
        { .name = "lte",
          .name_size = strlen("lte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lte
        },
        { .is_default = true,
          .run = present_repo_state,
          .obj = task
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}


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

static gsl_err_t parse_user(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndTask *self = obj;
    struct kndUser *user = self->shard->user;
    self->user = user;

    if (self->curr_locale_size) {
        self->locale = self->curr_locale;
        self->locale_size = self->curr_locale_size;
    }
    return knd_parse_select_user(self, rec, total_size);
}

static gsl_err_t parse_class_import(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *self = obj;
    struct kndClass *c = self->shard->repo->root_class;
    int err;

    if (DEBUG_TASK_LEVEL_TMP)
        knd_log(".. parsing the system class import: \"%.*s\"..", 64, rec);

    self->type = KND_UPDATE_STATE;
    err = knd_class_import(c, rec, total_size, self);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    return *total_size = 0, make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_select(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndClass *c = task->shard->repo->root_class;

    if (DEBUG_TASK_LEVEL_TMP)
        knd_log(".. parsing the system class select: \"%.*s\"", 64, rec);
    task->class = c;

    return knd_select_task(task->shard->repo, rec, total_size);
}

static gsl_err_t run_select_repo(void *obj, const char *name, size_t name_size)
{
    struct kndTask *task = obj;

    if (!name_size)  return make_gsl_err(gsl_FAIL);
    if (!memcmp(name, "/", 1)) {
        knd_log("== system repo selected!");
        task->repo = task->shard->repo;
        return make_gsl_err(gsl_OK);
    } else {
        knd_log("== user base repo selected!");
        task->repo = task->shard->user->repo;
        return make_gsl_err(gsl_OK);
    }

    return make_gsl_err(gsl_FAIL);
}

static gsl_err_t parse_select_repo(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *self = obj;
    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = run_select_repo,
            .obj = self
        },
        { .name = "_state",
          .name_size = strlen("_state"),
          .parse = parse_repo_state,
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
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
          .parse = parse_user,
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
          .parse = parse_select_repo,
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
