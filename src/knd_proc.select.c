#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include <gsl-parser.h>

#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_proc_call.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_mempool.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_text.h"
#include "knd_dict.h"
#include "knd_repo.h"
#include "knd_shard.h"
#include "knd_user.h"
#include "knd_output.h"

#define DEBUG_PROC_SELECT_LEVEL_0 0
#define DEBUG_PROC_SELECT_LEVEL_1 0
#define DEBUG_PROC_SELECT_LEVEL_2 0
#define DEBUG_PROC_SELECT_LEVEL_3 0
#define DEBUG_PROC_SELECT_LEVEL_TMP 1

struct LocalContext {
    struct kndRepo *repo;
    struct kndTask *task;
    struct kndProc *proc;
    struct kndProcEntry *entry;
};

static gsl_err_t run_get_proc(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepo *repo = ctx->repo;
    struct kndProc *proc;
    int err;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    ctx->proc = NULL;

    err = knd_get_proc(repo, name, name_size, &proc, ctx->task);
    if (err) return make_gsl_err_external(err);

    ctx->proc =  proc;
    ctx->entry = proc->entry;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_proc_selection(void *obj, const char *unused_var(val), size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndProc *proc = ctx->proc;
    knd_format format = task->ctx->format;
    struct kndOutput *out = task->out;
    int err;

    if (DEBUG_PROC_SELECT_LEVEL_2)
        knd_log(".. presenting proc selection..");

    if (!proc) return make_gsl_err(gsl_FAIL);

    out->reset(out);
    
    /* export BODY */
    err = knd_proc_export(proc, format, task, out);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t remove_proc(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndProc *proc = ctx->proc;
    struct kndTask *task = ctx->task;
    int err;

    if (DEBUG_PROC_SELECT_LEVEL_2)
        knd_log(".. removing proc: %.*s", name_size, name);

    if (!proc) {
        knd_log("-- remove operation: no proc selected");
        /*repo->log->reset(repo->log);
        err = repo->log->write(repo->log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = repo->log->write(repo->log, " class name not specified",
                               strlen(" class name not specified"));
                               if (err) return make_gsl_err_external(err);*/
        return make_gsl_err(gsl_NO_MATCH);
    }

    if (DEBUG_PROC_SELECT_LEVEL_2)
        knd_log("== proc to remove: \"%.*s\"\n",
                proc->name_size, proc->name);

    task->type = KND_COMMIT_STATE;
    if (!task->ctx->commit) {
        err = knd_commit_new(task->mempool, &task->ctx->commit);
        if (err) return make_gsl_err_external(err);

        task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->snapshot->num_commits,
                                                                memory_order_relaxed);
    }

    err = knd_proc_commit_state(proc, KND_REMOVED, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_inst_import(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndProcEntry *entry = ctx->entry;
    struct kndCommit *commit = task->ctx->commit;
    int err;

    if (!ctx->entry) {
        KND_TASK_LOG("no proc entry selected");
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    if (task->user_ctx) {
        if (entry->repo != task->user_ctx->repo) {
            knd_log(".. proc entry cloning..");
            err = knd_proc_entry_clone(ctx->entry, task->user_ctx->repo, &entry, task);
            if (err) {
                KND_TASK_LOG("failed to clone proc entry");
                return *total_size = 0, make_gsl_err_external(err);
            }
            ctx->entry = entry;
        }
    }

    switch (task->type) {
    case KND_GET_STATE:
        if (!commit) {
            err = knd_commit_new(mempool, &commit);
            if (err) return make_gsl_err_external(err);
            commit->orig_state_id = atomic_load_explicit(&task->repo->snapshot->num_commits, memory_order_relaxed);
            task->ctx->commit = commit;
        }
        break;
    default:
        break;
    }

    err = knd_import_proc_inst(entry, rec, total_size, task);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_proc_select(struct kndRepo *repo, const char *rec, size_t *total_size, struct kndTask *task)
{
    struct LocalContext ctx = {
        .task = task,
        .repo = repo
    };
    gsl_err_t parser_err;
    int err;

    if (DEBUG_PROC_SELECT_LEVEL_2)
        knd_log(".. proc selection: \"%.*s\"", 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_proc,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = remove_proc,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_proc_inst_import,
          .obj = &ctx
        },
        { .is_default = true,
          .run = present_proc_selection,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (!ctx.proc) {
        KND_TASK_LOG("no proc selected");
        return make_gsl_err(gsl_FAIL);
    }

    knd_state_phase phase;

    /* any commits happened? */
    switch (task->type) {
    case KND_COMMIT_STATE:
        phase = KND_UPDATED;
        if (task->phase == KND_REMOVED)
            phase = KND_REMOVED;
        err = knd_proc_commit_state(ctx.proc, phase, task);
        if (err) return make_gsl_err_external(err);
        break;
    default:
        break;
    }

    return make_gsl_err(gsl_OK);
}


