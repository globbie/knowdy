#include "knd_commit.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_task.h"
#include "knd_text.h"
#include "knd_repo.h"
#include "knd_user.h"
#include "knd_query.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_output.h"

#include <gsl-parser.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>

#define DEBUG_REPO_SELECT_LEVEL_1 0
#define DEBUG_REPO_SELECT_LEVEL_2 0
#define DEBUG_REPO_SELECT_LEVEL_3 0
#define DEBUG_REPO_SELECT_LEVEL_4 0
#define DEBUG_REPO_SELECT_LEVEL_5 0
#define DEBUG_REPO_SELECT_LEVEL_TMP 1

static int find_repo(struct kndRepo **result, const char *name, size_t name_size,
                     struct kndTask *task)
{
    struct kndRepo *repo;
    repo = knd_dict_get(task->repo_name_idx, name, name_size);
    if (!repo) return knd_NO_MATCH;
    *result = repo;
    return knd_OK;
}

static gsl_err_t get_repo(void *obj, const char *name, size_t name_size)
{
    struct kndTask *task = obj;
    struct kndQuery *query = task->ctx->query;
    struct kndRepoSnapshot *snapshot;
    int err;

    /* default system repo */
    struct kndRepo *repo = NULL;
    if (!name_size) return make_gsl_err(gsl_FAIL);

    /* special names */
    if (name_size == 1) {
        switch (*name) {
        case '/':
            repo = task->system_repo;
            break;
        case '~':
            repo = task->user_ctx->repo;
            break;
        default:
            err = find_repo(&repo, name, name_size, task);
            if (err) {
                return make_gsl_err(gsl_NO_MATCH);
            }
            break;
        }
    }
    if (!repo) {
        err = find_repo(&repo, name, name_size, task);
        if (err) {
            return make_gsl_err(gsl_NO_MATCH);
        }
    }

    task->repo = repo;
    task->user_ctx->repo = repo;

    query->type = KND_QUERY_GET;
    query->obj_type = KND_QUERY_OBJ_REPO;
    query->repo = repo;

    snapshot = atomic_load_explicit(&repo->snapshot, memory_order_relaxed);
    assert (snapshot != NULL);

    task->snapshot = snapshot;
    task->idxs = &repo->snapshot->idxs;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_selection(void *obj,
                                   const char *unused_var(name),
                                   size_t unused_var(name_size))
{
    struct kndTask *task = obj;
    // show a list of repos
    task->type = KND_SELECT_STATE;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_select(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo = ctx->repo ? ctx->repo : task->repo;
    return knd_class_select(repo, rec, total_size, task);
}

static gsl_err_t parse_class_import(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo = ctx->repo ? ctx->repo : task->repo;
    int err;

    if (task->type != KND_BULK_LOAD_STATE) {
        task->type = KND_COMMIT_STATE;
        if (!task->ctx->commit) {
            err = knd_commit_new(task->mempool, &task->ctx->commit);
            if (err) return make_gsl_err_external(err);

            task->ctx->commit->orig_state_id =\
                atomic_load_explicit(&task->snapshot->num_commits, memory_order_relaxed);
        }
    }
    return knd_class_import(repo, rec, total_size, task);
}

static int get_by_id(struct kndQuery *query, struct kndTask *task)
{
    struct kndRepo *repo = query->repo;
    int err;

    // TODO check view settings

    knd_log(">> {repo %.*s {query {type GET}}", repo->name_size, repo->name);

    switch (query->obj_type) {
    case KND_QUERY_OBJ_REPO:
        knd_log(".. presenting repo %.*s", repo->name_size, repo->name);
        break;
    case KND_QUERY_OBJ_CLASS:
        err = knd_class_export(query->cls, task->ctx->format, task);
        KND_TASK_ERR("class export failed");
    default:
        break;
    }

    return knd_OK;
}

static int select_by_attr_stms(struct kndQuery *query, struct kndTask *task)
{
    struct kndRepo *repo = query->repo;
    int err;

    err = knd_query_export_GSL(query, task);
    KND_TASK_ERR("failed to present a query");

    knd_log(">> SELECT query plan\n%.*s",
            task->out->buf_size, task->out->buf);

    // plan execution or async queue?
    knd_log(">> execute query plan");

    // export to GSP, pass ref

    return knd_OK;
}

gsl_err_t knd_parse_repo_select(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndQuery *query;
    gsl_err_t parser_err;
    int err;

    err = knd_query_new(&query, task->mempool);
    if (err) return make_gsl_err_external(err);
    task->ctx->query = query;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = get_repo,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "cls",
          .name_size = strlen("cls"),
          .parse = parse_class_import,
          .obj = task
        },
        { .name = "cls",
          .name_size = strlen("cls"),
          .parse = parse_class_select,
          .obj = task
        },
        { .is_default = true,
          .run = confirm_selection,
          .obj = task
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    switch (query->type) {
    case KND_QUERY_GET:
        err = get_by_id(query, task);
        if (err) {
            KND_TASK_LOG("failed to run a query");
            return make_gsl_err_external(err);
        }

        break;
    case KND_QUERY_SELECT:
        err = select_by_attr_stms(query, task);
        if (err) {
            KND_TASK_LOG("failed to select by attr stms");
            return make_gsl_err_external(err);
        }
        break;
    /* any commits happened? */
    case KND_QUERY_CREATE:
        // fall through
    case KND_QUERY_UPDATE:
        //err = knd_class_commit_state(ctx.cls->entry, task->phase, task);
        //KND_TASK_ERR("class commit failed");

        //err = knd_confirm_commit(repo, task);
        //KND_TASK_ERR("repo failed to confirm a commit");
        break;
    default:
        break;
    }

    return make_gsl_err(gsl_OK);    
}
