/**
 *   Copyright (c) 2011-present by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Graph DB, 
 *   and as such it is subject to the license stated
 *   in the LICENSE file which you have received 
 *   as part of this distribution.
 *
 *   Project homepage:
 *   <http://www.knowdy.org>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ----------
 *   knd_state.h
 *   Knowdy State Manager
 */

#pragma once

#include <stdatomic.h>
#include <time.h>
#include "knd_config.h"

struct kndState;
struct kndMemPool;
struct kndOutput;
struct kndRepoSnapshot;
struct kndTask;
struct kndCommitRef;

typedef enum knd_oper_t { KND_DEFAULT,
                          KND_CREATED,
                          KND_UPDATED,
                          KND_REMOVED,
                          KND_RESTORED } knd_oper_t;

typedef enum knd_obj_t { KND_STATE_DEFAULT,
                         KND_STATE_CLS,
                         KND_STATE_CLS_VAR,
                         KND_STATE_ATTR,
                         KND_STATE_ATTR_STM,
                         KND_STATE_CLS_DESC,
                         KND_STATE_CLS_INST,
                         KND_STATE_CLS_INST_INNER,
                         KND_STATE_PROC,
                         KND_STATE_PROC_INST
} knd_obj_t;

typedef enum knd_conflict_phase_t { KND_NO_CONFLICT,
                                    KND_CONFLICT_DETECTED,
                                    KND_CONFLICT_RESOLVED
} knd_conflict_phase_t;

struct kndStateRange
{
    size_t eq;
    size_t gt;
    size_t lt;
    size_t gte;
    size_t lte;
};

struct kndStateVal
{
    void *obj;
    const char *val;
    size_t val_size;
    long numid;
    void *ref;
};

struct kndStateConflict
{
    knd_conflict_phase_t phase;
    knd_obj_t obj_type;
    void *obj;

    struct kndCommitRef* _Atomic commits;
    atomic_size_t num_commits;
};

struct kndStateConflictRef
{
    struct kndStateConflict *conflict;
    struct kndStateConflictRef *next;
};

struct kndStateUpdate
{
    knd_oper_t oper_type;
    enum knd_obj_t obj_type;
    void *obj;

    struct kndCommit *commit;
    struct kndStateVal *source;
    struct kndStateVal *target;

    struct kndStateConflict *conflict;
    struct kndStateUpdate *next;
};

struct kndState
{
    size_t numid;

    struct kndCommit *commits;
    struct kndSet *commit_idx;
    size_t num_commits;

    struct kndState *next;
};

struct kndStateLedger
{
    struct kndCommit **commits;
    size_t num_commits;
    size_t max_commits;

    /** key: cls name
     *  value: state conflict 
     */
    struct kndSharedDict *cls_name_idx;

    /** key: attr stm id
     *  value: state conflict 
     */
    struct kndSharedSet *attr_stm_idx;

    struct kndSet *state_idx;
    size_t latest_state_id;
};

int knd_state_update_new(struct kndStateUpdate **result, struct kndMemPool *mempool);

int knd_state_val_new(struct kndStateVal **result, struct kndMemPool *mempool);
int knd_state_conflict_new(struct kndStateConflict **result, struct kndMemPool *mempool);
int knd_state_conflict_ref_new(struct kndStateConflictRef **result, struct kndMemPool *mempool);

int knd_state_index_commits(struct kndRepoSnapshot *snapshot, size_t collector_id,
                            struct kndStateLedger *ledger, struct kndTask *task);

int knd_state_reject_commits(struct kndStateConflict *conflict,
                             struct kndCommit *commit, struct kndTask *task);
int knd_state_resolve_conflicts(struct kndRepoSnapshot *snapshot,
                                struct kndStateLedger *ledger, struct kndTask *task);

int knd_state_ledger_new(struct kndStateLedger **result, size_t max_commits,
                         struct kndMemPool *shared_idx_mempool, struct kndMemPool *mempool);

int knd_state_read(struct kndRepoSnapshot *snapshot, struct kndStateRange *range, struct kndTask *task);

int knd_state_detect_conflicts(struct kndRepoSnapshot *snapshot, struct kndStateLedger *ledger,
                               struct kndTask *task);

int knd_state_join_conflicts(struct kndRepoSnapshot *snapshot, struct kndStateLedger *ledger,
                             struct kndTask *task);

int knd_state_ledger_fetch_conflict(struct kndStateLedger *ledger,
                                    struct kndCommit *commit, struct kndStateUpdate *update,
                                    struct kndStateConflict **result, struct kndTask *task);

int knd_state_update_collector_wal(struct kndRepoSnapshot *snapshot, struct kndStateLedger *ledger,
                                   struct kndTask *task);
int knd_state_advance(struct kndRepoSnapshot *snapshot, struct kndStateLedger *ledger,
                      struct kndTask *task);
