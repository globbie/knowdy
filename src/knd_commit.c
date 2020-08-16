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

int knd_commit_new(struct kndMemPool *mempool,
                   struct kndCommit **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                                sizeof(struct kndCommit), &page);                   RET_ERR();
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL,
                                     sizeof(struct kndCommit), &page);                     RET_ERR();
    }
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

    for (ref = state_refs; ref; ref = ref->next) {
        entry = ref->obj;
        state = ref->state;
        state->commit = commit;

        knd_log(".. resolve class inst %.*s", entry->name_size, entry->name);

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

int knd_dedup_commit(struct kndCommit *commit, struct kndTask *unused_var(task))
{
    // struct kndState *state;
    struct kndClassEntry *entry;
    struct kndStateRef *ref;

    for (ref = commit->class_state_refs; ref; ref = ref->next) {
        if (ref->state->phase == KND_REMOVED) {
            continue;
        }
        entry = ref->obj;
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

int knd_resolve_commit(struct kndCommit *commit, struct kndTask *task)
{
    struct kndState *state;
    struct kndClassEntry *entry;
    struct kndProcEntry *proc_entry;
    struct kndStateRef *ref;
    int err;

    if (DEBUG_COMMIT_LEVEL_TMP)
        knd_log(".. resolving commit #%zu (class state refs:%p)", commit->numid, commit->class_state_refs);
    
    for (ref = commit->class_state_refs; ref; ref = ref->next) {
        if (ref->state->phase == KND_REMOVED) {
            continue;
        }
        entry = ref->obj;
        if (!entry->class->is_resolved) {
            err = knd_class_resolve(entry->class, task);
            KND_TASK_ERR("failed to resolve class \"%.*s\"", entry->name_size, entry->name);
        }

        state = ref->state;
        state->commit = commit;
        if (!state->children) continue;

        err = resolve_class_inst_commit(state->children, commit, task);
        KND_TASK_ERR("failed to resolve commit of class insts");
    }

    /* PROCS */
    for (ref = commit->proc_state_refs; ref; ref = ref->next) {
        if (ref->state->phase == KND_REMOVED) {
            knd_log(".. proc to be removed");
            continue;
        }
        proc_entry = ref->obj;

        /* proc resolving */
        if (!proc_entry->proc->is_resolved) {
            err = knd_proc_resolve(proc_entry->proc, task);                       RET_ERR();
        }
    }
    return knd_OK;
}
