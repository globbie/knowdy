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

int knd_commit_confirm(struct kndCommit *commit, struct kndTask *task)
{
    struct kndRepoSnapshot *snapshot = commit->snapshot;
    struct kndRepo *repo = snapshot->repo;
    int err;

    assert(commit != NULL);

    if (DEBUG_COMMIT_LEVEL_TMP) {
        knd_log(">> {repo %.*s} to confirm {commit #%zu}",
                repo->name_size, repo->name, commit->numid);
    }

    err = knd_commit_resolve(commit, snapshot, task);
    KND_TASK_ERR("failed to resolve commit #%zu", commit->numid);

    /* check any doublets in concept definitions */
    err = knd_commit_dedup(commit, snapshot, task);
    KND_TASK_ERR("failed to dedup commit #%zu", commit->numid);

    /* if conflicts with current state are found, describe these in reply */
    //err = knd_commit_check_conflicts(commit, snapshot, task);
    //KND_TASK_ERR("commit conflicts detected, please get the latest repo updates");

    commit->numid = task->num_commits++;

    /* append a persistent WAL record (task local) */
    err = knd_commit_update_task_wal(commit, snapshot, task->id, task);
    KND_TASK_ERR("failed to update task wal with {commit %zu}", commit->numid);

    return knd_OK;
}

gsl_err_t knd_commit_process(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndCommit *commit;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_COMMIT_LEVEL_TMP) {
        knd_log(">> new commit by {task #%zu}", task->id);
    }

    err = knd_commit_new(&commit, task->mempool);
    if (err) return make_gsl_err_external(err);

    task->type = KND_TASK_COMMIT;
    task->ctx->commit = commit;

    struct gslTaskSpec specs[] = {
        { .type = GSL_GET_ARRAY_STATE,
          .name = "locale",
          .name_size = strlen("locale"),
          .parse = knd_text_parse_locale,
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

    err = knd_commit_confirm(commit, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

int knd_commit_new(struct kndCommit **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(KND_SMALL_MEMPAGE_SIZE >= sizeof(struct kndCommit));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndCommit));
    *result = page;
    return knd_OK;
}

int knd_commit_ref_new(struct kndCommitRef **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndCommitRef));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndCommitRef));
    *result = page;
    return knd_OK;
}
