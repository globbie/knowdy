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

static int find_repo(struct kndRepo **result, const char *name, size_t name_size, struct kndTask *task)
{
    struct kndRepo *repo;
    assert (task->repo_name_idx != NULL);

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
    struct kndQuery *query = task->ctx->query;

    switch (task->type) {
    case KND_TASK_QUERY:
        query->type = KND_QUERY_SELECT;
        break;
    default:
        break;
    }
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

    if (task->type != KND_TASK_BULK_LOAD) {
        task->type = KND_TASK_COMMIT;
        if (!task->ctx->commit) {
            err = knd_commit_new(&task->ctx->commit, task->mempool);
            if (err) return make_gsl_err_external(err);

            task->ctx->commit->orig_state_id =\
                atomic_load_explicit(&task->snapshot->num_commits, memory_order_relaxed);
        }
    }
    return knd_class_import(repo, rec, total_size, task);
}

gsl_err_t knd_parse_repo_select(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    gsl_err_t parser_err;

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

    return make_gsl_err(gsl_OK);    
}
