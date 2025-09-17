#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_COMMIT_LEVEL_0 0
#define DEBUG_COMMIT_LEVEL_1 0
#define DEBUG_COMMIT_LEVEL_2 0
#define DEBUG_COMMIT_LEVEL_3 0
#define DEBUG_COMMIT_LEVEL_TMP 1

gsl_err_t knd_commit_run(void *obj, const char *unused_var(rec), size_t *unused_var(total_size))
{
    struct kndTask *task = obj;
    //struct kndCommit *commit;
    //gsl_err_t parser_err;
    // int err;

    task->type = KND_TASK_QUERY;
    //task->ctx->commit = commit;

    /*    struct gslTaskSpec specs[] = {
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
        { .name = "user",
          .name_size = strlen("user"),
          .parse = knd_parse_select_user,
          .obj = task
        },
        { .name = "repo",
          .name_size = strlen("repo"),
          .parse = knd_parse_repo_select,
          .obj = task
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    switch (parser_err.code) {
    case gsl_OK:
        break;
    case gsl_NO_MATCH:
        KND_TASK_LOG("unknown {tag %.*s}", parser_err.val_size, parser_err.val);
        return make_gsl_err(gsl_NO_MATCH);
    default:
        return parser_err;
    }

    switch (query->type) {
    case KND_QUERY_GET:
        err = knd_query_obj_export(query, task);
        if (err) {
            KND_TASK_LOG("failed to present a requested object");
            return make_gsl_err_external(err);
        }
        break;
    case KND_QUERY_SELECT:
        err = query_plan(query, task);
        if (err) {
            KND_TASK_LOG("failed to plan a query");
            return make_gsl_err_external(err);
        }

        if (query->complexity < query->max_complexity) {
            err = knd_query_match_export(query, task);
            if (err) {
                KND_TASK_LOG("failed to present the matching results of a query");
                return make_gsl_err_external(err);
            }
            return make_gsl_err(gsl_OK);
        }

        // TODO: signal the need for a long-running task

        break;
    default:
        break;
    }
    */   
    return make_gsl_err(gsl_OK);
}

int knd_commit_new(struct kndCommit **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndCommit));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndCommit));
    *result = page;
    (*result)->numid = 1;
    return knd_OK;
}

static int resolve_class_inst_commit(struct kndStateRef *state_refs, struct kndCommit *commit, struct kndTask *task)
{
    struct kndState *state;
    struct kndClassInstEntry *entry;
    struct kndStateRef *ref;
    int err;

    FOREACH (ref, state_refs) {
        entry = ref->obj;
        state = ref->state;
        state->commit = commit;

        switch (state->phase) {
        case KND_CREATED:
            if (!entry->inst->is_resolved) {
                err = knd_class_inst_resolve(entry->inst, task);
                KND_TASK_ERR("failed to resolve class inst %.*s", entry->name_size, entry->name);
            }
            break;
        default:
            // TODO: resolve inst attrs
            // state->children
            break;
        }
    }
    return knd_OK;
}

int knd_commit_dedup(struct kndCommit *commit, struct kndTask *unused_var(task))
{
    // struct kndState *state;
    struct kndClassEntry *entry;
    struct kndStateRef *ref;

    FOREACH (ref, commit->class_state_refs) {
        if (ref->state->phase == KND_REMOVED) {
            continue;
        }
        entry = ref->obj;

        if (DEBUG_COMMIT_LEVEL_2)
            knd_log(".. dedup class \"%.*s\"", entry->name_size, entry->name);

        //err = knd_class_dedup(entry->class, task);
        //KND_TASK_ERR("failed to dedup class \"%.*s\"", entry->name_size, entry->name);

        /*state = ref->state;
        state->commit = commit;
        if (!state->children) continue;

        err = dedup_class_inst_commit(state->children, commit, task);
        KND_TASK_ERR("failed to dedup commit of class insts");
        */
    }
    return knd_OK;
}

int knd_commit_resolve(struct kndCommit *commit, struct kndTask *task)
{
    struct kndState *state;
    struct kndProcEntry *proc_entry;
    struct kndStateRef *ref;
    int err;

    if (DEBUG_COMMIT_LEVEL_TMP) {
        knd_log(".. resolving {commit #%zu}", commit->numid);
    }

    FOREACH (ref, commit->class_state_refs) {
        if (ref->state->phase == KND_REMOVED) {
            continue;
        }
        state = ref->state;
        state->commit = commit;
        if (!state->children) continue;

        err = resolve_class_inst_commit(state->children, commit, task);
        KND_TASK_ERR("failed to resolve commit of class insts");
    }

    /* PROCS */
    FOREACH (ref, commit->proc_state_refs) {
        if (ref->state->phase == KND_REMOVED) {
            // knd_log(".. proc to be removed");
            continue;
        }
        proc_entry = ref->obj;

        /* proc resolving */
        if (!proc_entry->proc->is_resolved) {
            err = knd_proc_resolve(proc_entry->proc, task);
            KND_TASK_ERR("failed to resolve proc commit");
        }
    }
    return knd_OK;
}
