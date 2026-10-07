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

#define DEBUG_COMMIT_INDEX_LEVEL_0 0
#define DEBUG_COMMIT_INDEX_LEVEL_1 0
#define DEBUG_COMMIT_INDEX_LEVEL_2 0
#define DEBUG_COMMIT_INDEX_LEVEL_3 0
#define DEBUG_COMMIT_INDEX_LEVEL_TMP 1

static int index_commit_update(struct kndCommit *commit, struct kndStateUpdate *update,
                               struct kndStateLedger *ledger, struct kndTask *task)
{
    struct kndStateConflict *conflict;
    struct kndStateConflictRef *ref;
    int err;

    err = knd_state_ledger_fetch_conflict(ledger, commit, update, &conflict, task);
    KND_TASK_ERR("failed to fetch commits conflict");

    err = knd_state_conflict_ref_new(&ref, task->mempool);
    KND_TASK_ERR("failed to alloc a state conflict ref");

    ref->conflict = conflict;
    ref->next = commit->conflicts;
    commit->conflicts = ref;
    commit->num_conflicts++;

    return knd_OK;
}

int knd_commit_index(struct kndCommit *commit, struct kndStateLedger *ledger, struct kndTask *task)
{
    struct kndStateUpdate *update;
    int err;

    if (DEBUG_COMMIT_INDEX_LEVEL_2) {
        knd_log(".. indexing {commit #%zu}", commit->numid);
    }

    FOREACH (update, commit->updates) {
        err = index_commit_update(commit, update, ledger, task);
        KND_TASK_ERR("failed to index commit update");
    }
    return knd_OK;
}

int knd_state_index_commits(struct kndRepoSnapshot *snapshot, size_t collector_id,
                            struct kndStateLedger *ledger, struct kndTask *task)
{
    struct kndCommit *commit;
    int err;

    if (DEBUG_COMMIT_INDEX_LEVEL_TMP) {
        knd_log(".. indexing commits of {collector #%zu}", collector_id);
    }

    for (size_t i = 0; i < task->num_collector_commits; i++) {
        commit = task->collector_commits[i];

        err = knd_commit_index(commit, ledger, task);
        KND_TASK_ERR("failed to index a commit");
    }
   
    return knd_OK;
}
