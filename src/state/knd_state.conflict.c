#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_utils.h"

#define DEBUG_STATE_CONFLICT_LEVEL_1 0
#define DEBUG_STATE_CONFLICT_LEVEL_2 0
#define DEBUG_STATE_CONFLICT_LEVEL_3 0
#define DEBUG_STATE_CONFLICT_LEVEL_4 0
#define DEBUG_STATE_CONFLICT_LEVEL_5 0
#define DEBUG_STATE_CONFLICT_LEVEL_TMP 1

int knd_state_ledger_add_conflict(struct kndStateLedger *ledger,
                                  struct kndStateConflict *conflict, struct kndTask *task)
{
    return knd_OK;
}

int knd_state_conflict_add_commit(struct kndStateConflict *conflict,
                                  struct kndCommit *commit, struct kndTask *task)
{
    struct kndCommitRef *refs;
    struct kndCommitRef *ref;
    int err;

    err = knd_commit_ref_new(&ref, commit, task->mempool);
    KND_TASK_ERR("failed to alloc a commit ref");

    ref->commit = commit;

    do {
        refs = atomic_load_explicit(&conflict->commits, memory_order_acquire);
        ref->next = refs;
    } while (!atomic_compare_exchange_weak(&conflict->commits, &refs, ref));

    atomic_fetch_add_explicit(&conflict->num_commits, 1, memory_order_relaxed);

    return knd_OK;
}

static int reject_commits(struct kndStateConflict *conflict,
                          struct kndCommit *commit, struct kndTask *task)
{
    struct kndCommitRef *refs;
    struct kndCommitRef *ref;
    int err;

    refs = atomic_load_explicit(&conflict->commits, memory_order_acquire);

    FOREACH (ref, refs) {
        /* the winner */
        if (ref->commit == commit) continue;
        ref->commit->phase = KND_CONFLICT_STATE;
    }
    return knd_OK;
}

       
        // foreach conflict of a winner - reject all other commits        
//        err = reject_commits(conflict, commit, task);
//        KND_TASK_ERR("failed to reject commits");
 
int knd_state_resolve_conflicts(struct kndRepoSnapshot *snapshot,
                                struct kndStateLedger *ledger,
                                struct kndTask *task)
{
    struct kndRepo *repo = snapshot->repo;
    struct kndCommit *commit, *commits;
    int err;

    if (DEBUG_STATE_CONFLICT_LEVEL_TMP) {
        knd_log("!! The Arbiter to resolve conflicts of {repo %.*s}", repo->name_size, repo->name);
    }

    // iterate over all commits, starting from top priorities
    FOREACH (commit, commits) {
        
        // if commit is a winner (was not cancelled by any of the upper commits)
        
    }

    return knd_OK;
}

int knd_state_detect_conflicts(struct kndRepoSnapshot *snapshot, struct kndStateLedger *ledger,
                               struct kndTask *task)
{

    // iterate over commits, mark non-conflicting ones (compare with other collectors)

    
    // sort conflicting commits by rating, timestamp
    // for later processing by the arbiter
    // NB: use facets

    return knd_OK;
}


