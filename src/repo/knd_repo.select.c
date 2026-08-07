#include "knd_commit.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_task.h"
#include "knd_steward.h"
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

static int find_repo(struct kndRepo **result, const char *name, size_t name_size, struct kndTask *task)
{
    struct kndRepo *repo;
    int err;

    assert (task->idxs.repo_name_idx != NULL);

    err = knd_dict_get(task->idxs.repo_name_idx, name, name_size, (void**)&repo, task);
    if (err) return knd_NO_MATCH;

    *result = repo;
    return knd_OK;
}

static gsl_err_t get_repo_snapshot(void *obj, const char *name, size_t name_size)
{
    struct kndTask *task = obj;
    struct kndSteward *steward = task->steward;
    struct kndRepo *repo = NULL;
    int err;

    assert (steward != NULL);

    /* default system repo */
    if (!name_size) return make_gsl_err(gsl_FAIL);

    /* special names */
    if (name_size == 1) {
        switch (*name) {
        case '/':
            repo = steward->repo; 
            break;
        case '~':
            repo = task->user_ctx->repo;
            break;
        default:
            break;
        }
    } else {
        err = find_repo(&repo, name, name_size, task);
        if (err) {
            return make_gsl_err(gsl_NO_MATCH);
        }
    }

    assert (repo != NULL);
    assert (repo->snapshot != NULL);

    switch (task->type) {
    case KND_TASK_QUERY:
        task->ctx->query->snapshot = repo->snapshot;
        break;
    case KND_TASK_COMMIT:
        task->ctx->commit->snapshot = repo->snapshot;
        break;
    default:
        break;
    }

    if (DEBUG_REPO_SELECT_LEVEL_3) {
        knd_log("got a snapshot of {repo %.*s}", repo->name_size, repo->name);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_selection(void *obj,
                                   const char *unused_var(name),
                                   size_t unused_var(name_size))
{
    struct kndTask *task = obj;
    struct kndQuery *query = task->ctx->query;

    switch (task->type) {
    case KND_TASK_QUERY:
        /* multiple results expected */
        query->type = KND_QUERY_SELECT;
        break;
    default:
        break;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_cls_select(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndCommit *commit;
    struct kndQuery *query;
    int err;

    switch (task->type) {
    case KND_TASK_COMMIT:
        commit = task->ctx->commit;

        assert (commit != NULL);
        assert (commit->snapshot != NULL);

        err = knd_cls_commit_select(rec, total_size, commit, task);
        if (err) return make_gsl_err_external(err);
        break;
    case KND_TASK_QUERY:
        query = task->ctx->query;

        assert (query != NULL);
        assert (query->snapshot != NULL);

        query->type = KND_QUERY_SELECT;
        err = knd_cls_query_select(rec, total_size, query, task);
        if (err) return make_gsl_err_external(err);
        break;
    default:
        break;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_cls_import(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndRepoSnapshot *snapshot = NULL;
    int err;

    switch (task->role) {
    case KND_AGENT_WRITER:
        snapshot = task->ctx->commit->snapshot;
        break;
    default:
        knd_log("import operations not allowed for task {role %d}", task->role);
        return make_gsl_err(gsl_FORMAT);
    }

    assert (snapshot != NULL);
    err = knd_repo_cls_import(rec, total_size, snapshot, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_parse_repo_select(void *obj, const char *rec, size_t *total_size)
{
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = get_repo_snapshot,
          .obj = obj
        },
        { .type = GSL_SET_STATE,
          .name = "cls",
          .name_size = strlen("cls"),
          .parse = parse_cls_import,
          .obj = obj
        },
        { .name = "cls",
          .name_size = strlen("cls"),
          .parse = parse_cls_select,
          .obj = obj
        },
        { .is_default = true,
          .run = confirm_selection,
          .obj = obj
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);    
}
