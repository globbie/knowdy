#include "knd_task.h"

#include "knd_class.h"
#include "knd_proc.h"
#include "knd_repo.h"
#include "knd_shard.h"
#include "knd_user.h"
#include "knd_commit.h"
#include "knd_cache.h"
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
            self->ctx->format = (knd_format)i;
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

static gsl_err_t parse_format(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *self = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_format,
          .obj = self
        },
        { .name = "indent",
          .name_size = strlen("indent"),
          .parse = gsl_parse_size_t,
          .obj = &self->ctx->format_indent
        },
        { .name = "depth",
          .name_size = strlen("depth"),
          .parse = gsl_parse_size_t,
          .obj = &self->ctx->max_depth
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t run_set_locale(void *obj, const char *name, size_t name_size)
{
    struct kndTask *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size > sizeof(self->ctx->locale)) return make_gsl_err(gsl_FORMAT);

    memcpy(self->ctx->locale, name, name_size);
    self->ctx->locale_size = name_size;

    /* TODO check locale
    for (size_t i = 0; i < sizeof knd_format_names / sizeof knd_format_names[0]; i++) {
        const char *format_str = knd_format_names[i];
        assert(format_str != NULL);

        size_t format_str_size = strlen(format_str);
        if (name_size != format_str_size) continue;

        if (!memcmp(format_str, name, name_size)) {
            self->ctx->format = (knd_format)i;
            return make_gsl_err(gsl_OK);
        }
    }

    err = self->log->write(self->log, name, name_size);
    if (err) return make_gsl_err_external(err);
    err = self->log->write(self->log, " locale not supported",
                           strlen(" locale not supported"));
    if (err) return make_gsl_err_external(err);
    */

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_locale(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *self = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_locale,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_class_import(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    int err;

    if (DEBUG_TASK_LEVEL_2)
        knd_log(".. parsing the system class import: \"%.*s\"..", 64, rec);

    task->type = KND_COMMIT_STATE;
    if (!task->ctx->commit) {
        err = knd_commit_new(task->user_ctx->mempool, &task->ctx->commit);
        if (err) return make_gsl_err_external(err);

        task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->snapshots->num_commits,
                                                                memory_order_relaxed);
    }

    return knd_class_import(task->repo, rec, total_size, task);
}

static gsl_err_t parse_class_select(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    // no explicit repo selection -> defaults to system repo
    task->user_ctx->repo = task->repo;

    if (DEBUG_TASK_LEVEL_3)
        knd_log(".. parsing the system repo class selection: \"%.*s\"", 64, rec);
    return knd_class_select(task->repo, rec, total_size, task);
}

static gsl_err_t parse_proc_import(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndRepo *repo = task->repo;
    int err;

    if (DEBUG_TASK_LEVEL_2)
        knd_log(".. parsing the system proc import: \"%.*s\"..", 64, rec);

    task->type = KND_COMMIT_STATE;
    if (!task->ctx->commit) {
        err = knd_commit_new(task->mempool, &task->ctx->commit);
        if (err) return make_gsl_err_external(err);

        task->ctx->commit->orig_state_id = atomic_load_explicit(&repo->snapshots->num_commits, memory_order_relaxed);
    }
    return knd_proc_import(task->repo, rec, total_size, task);
}

static gsl_err_t parse_proc_select(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    if (DEBUG_TASK_LEVEL_2)
        knd_log(".. parsing the system proc select: \"%.*s\"", 64, rec);

    return knd_proc_select(task->repo, rec, total_size, task);
}

static gsl_err_t parse_update(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *self = obj;

    self->type = KND_LIQUID_STATE;

    struct gslTaskSpec specs[] = {
        { .name = "user",
          .name_size = strlen("user"),
          .parse = knd_parse_select_user,
          .obj = self
        }
    };
    self->type = KND_LIQUID_STATE;
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_snapshot_task(void *obj, const char *unused_var(rec), size_t *total_size)
{
    struct kndTask *task = obj;
    int err;

    task->type = KND_SNAPSHOT_STATE;
    err = knd_repo_snapshot(task->repo, task);
    if (err) {
        KND_TASK_LOG("failed to build a snapshot of sys repo");
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }
    return *total_size = 0, make_gsl_err(gsl_OK);
}

gsl_err_t knd_parse_task(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    gsl_err_t parser_err;
    int err;

    struct gslTaskSpec specs[] = {
        { .name = "locale",
          .name_size = strlen("locale"),
          .parse = parse_locale,
          .obj = task
        },
        { .name = "format",
          .name_size = strlen("format"),
          .parse = parse_format,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "user",
          .name_size = strlen("user"),
          .parse = knd_create_user,
          .obj = task
        },
        { .name = "user",
          .name_size = strlen("user"),
          .parse = knd_parse_select_user,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_import,
          .obj = task
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_import,
          .obj = task
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_select,
          .obj = task
        },
        { .name = "repo",
          .name_size = strlen("repo"),
          .parse = knd_parse_repo,
          .obj = task
        },
        { .name = "update",
          .name_size = strlen("update"),
          .parse = parse_update,
          .obj = task
        },
        { .name = "_snapshot",
          .name_size = strlen("_snapshot"),
          .parse = parse_snapshot_task,
          .obj = task
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    switch (parser_err.code) {
    case gsl_OK:
        break;
    case gsl_NO_MATCH:
        KND_TASK_LOG("unknown tag: %.*s", parser_err.val_size, parser_err.val);
        // fall through
    default:
        return parser_err;
    }

    /* any commits? */
    switch (task->type) {
    case KND_COMMIT_STATE:
        err = knd_confirm_commit(task->user_ctx->repo, task);
        if (err) {
            parser_err = make_gsl_err(gsl_FAIL);
            knd_log("commit confirm err:%d code:%d", err, parser_err.code);
            return parser_err;
        }
        // TODO
        // knd_log(".. building report for commit %zu", task->ctx->commit->numid);
        break;
    default:
        task->ctx->phase = KND_COMPLETE;
        break;
    }
    return make_gsl_err(gsl_OK);
}

