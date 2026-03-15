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

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
};

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

static gsl_err_t get_repo(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndQuery *query = task->ctx->query;
    int err;

    /* default system repo */
    if (!name_size) return make_gsl_err(gsl_FAIL);

    /* special names */
    if (name_size == 1) {
        switch (*name) {
        case '/':
            knd_log("== {sys-repo %p}", repo);
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

    assert (repo != NULL);
    assert (repo->snapshot != NULL);

    query->type = KND_QUERY_GET;
    query->obj_type = KND_QUERY_OBJ_REPO;
    query->repo = repo;

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
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    return knd_class_select(rec, total_size, ctx->repo, task);
}

static gsl_err_t parse_class_import(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepo *repo = ctx->repo;
    struct kndTask *task = ctx->task;
    struct kndClass *cls;
    int err;

    if (task->type != KND_TASK_BULK_LOAD) {
        task->type = KND_TASK_COMMIT;
        if (!task->ctx->commit) {
            err = knd_commit_new(&task->ctx->commit, task->mempool);
            if (err) return make_gsl_err_external(err);

            //task->ctx->commit->orig_state_id =                        \
            //    atomic_load_explicit(&task->snapshot->num_commits, memory_order_relaxed);
        }
    }

    err = knd_class_import(rec, total_size, &cls, repo, task);
    if (err) return make_gsl_err_external(err);

    /* assign a unique class entry id */
    cls->entry->numid = task->idxs.cls_id_count++;
    knd_uid_create(cls->entry->numid, cls->entry->id, &cls->entry->id_size);

    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_parse_repo_select(void *obj, const char *rec, size_t *total_size)
{
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = get_repo,
          .obj = obj
        },
        { .type = GSL_SET_STATE,
          .name = "cls",
          .name_size = strlen("cls"),
          .parse = parse_class_import,
          .obj = obj
        },
        { .name = "cls",
          .name_size = strlen("cls"),
          .parse = parse_class_select,
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
