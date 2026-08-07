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

#define DEBUG_COMMIT_RESOLVE_LEVEL_0 0
#define DEBUG_COMMIT_RESOLVE_LEVEL_1 0
#define DEBUG_COMMIT_RESOLVE_LEVEL_2 0
#define DEBUG_COMMIT_RESOLVE_LEVEL_3 0
#define DEBUG_COMMIT_RESOLVE_LEVEL_TMP 1

int knd_commit_resolve(struct kndCommit *commit, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndState *state;
    struct kndProcEntry *proc_entry;
    struct kndStateRef *ref;
    int err;

    if (DEBUG_COMMIT_RESOLVE_LEVEL_TMP) {
        knd_log(".. resolving {commit #%zu}", commit->numid);
    }

    /*FOREACH (ref, commit->class_state_refs) {
        if (ref->state->phase == KND_REMOVED) {
            continue;
        }
        state = ref->state;
        state->commit = commit;
        if (!state->children) continue;

        err = resolve_class_inst_commit(state->children, commit, snapshot, task);
        KND_TASK_ERR("failed to resolve commit of class insts");
        }
    */

    return knd_OK;
}

int knd_commit_dedup(struct kndCommit *commit, struct kndRepoSnapshot *snapshot,
                     struct kndTask *unused_var(task))
{
    struct kndRepo *repo = snapshot->repo;

    // TODO: each new concept (class, proc ..) must be unique,
    // make sure no duplicate definitions exist in the schema

    if (DEBUG_COMMIT_RESOLVE_LEVEL_TMP) {
        knd_log(".. deduplication of {repo %.*s {commit #%zu}}",
                commit->numid, repo->name_size, repo->name);
    }

    return knd_OK;
}
