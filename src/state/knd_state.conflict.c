#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_shared_dict.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_utils.h"

#define DEBUG_STATE_CONFLICT_LEVEL_1 0
#define DEBUG_STATE_CONFLICT_LEVEL_2 0
#define DEBUG_STATE_CONFLICT_LEVEL_3 0
#define DEBUG_STATE_CONFLICT_LEVEL_4 0
#define DEBUG_STATE_CONFLICT_LEVEL_5 0
#define DEBUG_STATE_CONFLICT_LEVEL_TMP 1

static int compare_commits_by_priority_and_time(const void *a, const void *b)
{
    struct kndCommit **obj1, **obj2;

    obj1 = (struct kndCommit**)a;
    obj2 = (struct kndCommit**)b;

    if ((*obj1)->priority == (*obj2)->priority) return 0;
    if ((*obj1)->priority > (*obj2)->priority) return 1;

    return -1;
}

static int conflict_add_commit(struct kndStateConflict *conflict, struct kndCommit *commit,
                               struct kndStateUpdate *update, struct kndTask *task)
{
    struct kndCommitRef *ref;
    struct kndCommitRef *refs;
    int err;

    err = knd_commit_ref_new(&ref, task->mempool);
    KND_TASK_ERR("failed to alloc a commit ref");
    ref->commit = commit;
    ref->update = update;

    do {
        refs = atomic_load_explicit(&conflict->commits, memory_order_acquire);
        ref->next = refs;
    } while (!atomic_compare_exchange_weak(&conflict->commits, &refs, ref));

    atomic_fetch_add_explicit(&conflict->num_commits, 1, memory_order_relaxed);

    return knd_OK;
}

static int add_new_cls_conflict(struct kndSharedDict *cls_name_idx,
                                struct kndCommit *commit, struct kndStateUpdate *update,
                                struct kndClassEntry *entry, struct kndStateConflict **result,
                                struct kndTask *task)
{
    struct kndStateConflict *conflict;
    void *obj;
    int err;

    knd_log(".. add new cls conflict..");

    err = knd_state_conflict_new(&conflict, task->mempool);
    KND_TASK_ERR("failed to alloc a state conflict");

    err = knd_shared_dict_set(cls_name_idx, entry->name, entry->name_size, (void*)conflict, &obj);
    switch (err) {
    case knd_OK:
        break;
    case knd_CONFLICT:
        // TODO free alloc'd conflict
        conflict = obj;
        break;
    default:
        KND_TASK_ERR("failed to add conflict to cls_name_idx");
    }

    err = conflict_add_commit(conflict, commit, update, task);
    KND_TASK_ERR("failed to add a commit to conflict");

    *result = conflict;
    return knd_OK;
}

static int fetch_cls_conflict(struct kndStateLedger *ledger,
                              struct kndCommit *commit, struct kndStateUpdate *update,
                              struct kndStateConflict **result, struct kndTask *task)
{
    struct kndSharedDict *cls_name_idx = ledger->cls_name_idx;
    struct kndClassEntry *entry = update->obj;
    struct kndStateConflict *conflict = NULL;
    void *obj;
    int err;

    assert (entry != NULL);
    assert (entry->name_size != 0);

    err = knd_shared_dict_get(cls_name_idx, entry->name, entry->name_size, &obj);
    switch (err) {
    case knd_OK:
        conflict = obj;
        break;
    case knd_NO_MATCH:
        err = add_new_cls_conflict(cls_name_idx, commit, update, entry, result, task);
        KND_TASK_ERR("failed to add a new cls conflict");
        return knd_OK;
    default:
        return err;
    }

    update->conflict = conflict;

    err = conflict_add_commit(conflict, commit, update, task);
    KND_TASK_ERR("failed to add a commit to conflict");
    
    return knd_OK;
}

int knd_state_ledger_fetch_conflict(struct kndStateLedger *ledger,
                                    struct kndCommit *commit, struct kndStateUpdate *update,
                                    struct kndStateConflict **result, struct kndTask *task)
{
    int err;

    switch (update->obj_type) {
    case KND_STATE_CLS:
        err = fetch_cls_conflict(ledger, commit, update, result, task);
        KND_TASK_ERR("failed to fetch cls conflict");
        break;
    default:
        break;
    }

    return knd_OK;
}


/* Collector */
static int detect_conflicts(struct kndCommit *commit, struct kndTask *task)
{
    struct kndStateConflict *conflict;
    struct kndStateConflictRef *ref;
    size_t num_commits;
    int err;

    FOREACH (ref, commit->conflicts) {
        conflict = ref->conflict;

        num_commits = atomic_load_explicit(&conflict->num_commits, memory_order_relaxed);

        if (num_commits == 1) continue;

        commit->phase = KND_CONFLICT_STATE;
        return knd_OK;
    }

    commit->phase = KND_CONFIRMED_STATE;
    return knd_OK;
}


/* Collector */
int knd_state_detect_conflicts(struct kndRepoSnapshot *snapshot, struct kndStateLedger *ledger,
                               struct kndTask *task)
{
    struct kndCommit *commit;
    int err;

    for (size_t i = 0; i < task->num_collector_commits; i++) {
        commit = task->collector_commits[i];

        err = detect_conflicts(commit, task);
        KND_TASK_ERR("failed to detect conflicts");
    }

    return knd_OK;
}

/* Arbiter  TODO: find concurrent solution */
int knd_state_join_conflicts(struct kndRepoSnapshot *snapshot, struct kndStateLedger *ledger,
                             struct kndTask *task)
{
    struct kndCommit *commit;
    int err;

    for (size_t i = 0; i < task->num_collector_commits; i++) {
        commit = task->collector_commits[i];

        if (commit->phase != KND_CONFLICT_STATE) continue;

        ledger->commits[ledger->num_commits] = commit;
        ledger->num_commits++;
    }
 
    return knd_OK;
}

/* Arbiter */
static int resolve_conflicts(struct kndCommit *commit, struct kndTask *task)
{
    struct kndStateConflict *conflict;
    struct kndStateConflictRef *ref;
    int err;

    FOREACH (ref, commit->conflicts) {
        conflict = ref->conflict;

        /* some other commit was a winner here */
        if (conflict->phase == KND_CONFLICT_RESOLVED) {
            commit->phase = KND_REJECTED_STATE;
            return knd_OK;
        }
    }

    /* the winner takes it all */
    commit->phase = KND_CONFIRMED_STATE;
    FOREACH (ref, commit->conflicts) {
        ref->conflict->phase = KND_CONFLICT_RESOLVED;
    }
    return knd_OK;
}

/* Arbiter */
int knd_state_resolve_conflicts(struct kndRepoSnapshot *snapshot,
                                struct kndStateLedger *ledger, struct kndTask *task)
{
    struct kndRepo *repo = snapshot->repo;
    struct kndCommit *commit, *commits;
    int err;

    if (DEBUG_STATE_CONFLICT_LEVEL_TMP) {
        knd_log("!! The Arbiter to resolve conflicts of {repo %.*s}", repo->name_size, repo->name);
    }

    qsort(ledger->commits, ledger->num_commits, sizeof(struct kndCommit*), compare_commits_by_priority_and_time);

    for (size_t i = 0; i < ledger->num_commits; i++) {
        commit = ledger->commits[i];

        err = resolve_conflicts(commit, task);
        KND_TASK_ERR("failed to resolve conflicts");
    }

    return knd_OK;
}
