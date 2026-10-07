#pragma once

#include <time.h>
#include "knd_output.h"
#include "knd_state.h"
#include "knd_config.h"

struct kndMemPool;
struct kndTask;
struct kndRepo;
struct kndStateRange;
struct kndRepoSnapshot;
struct kndStateLedger;
struct kndStorageWal;

typedef enum knd_commit_phase_t { KND_INIT_STATE,
                                  KND_CONTRADICTORY_STATE,
                                  KND_CONFLICT_STATE,
                                  KND_REJECTED_STATE,
                                  KND_CONFIRMED_STATE,
                                  KND_PERSISTENT_STATE,
                                  KND_REPLICATED_STATE
} knd_commit_phase_t;

struct kndCommit
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;
    knd_commit_phase_t phase;

    size_t priority;
    struct timespec start_ts;

    char *rec;
    size_t rec_size;

    struct kndRepoSnapshot *snapshot;
    struct kndStorageWal *wal;
    struct kndState *state;

    struct kndStateUpdate *updates;
    struct kndStateUpdate *update_tail;
    size_t num_updates;

    struct kndStateConflictRef *conflicts;
    size_t num_conflicts;

    struct kndCommit *prev;
    struct kndCommit *next;
};

struct kndCommitRef {
    struct kndCommit *commit;
    struct kndStateUpdate *update;
    struct kndCommitRef *next;
};

static inline void knd_commit_append_update(struct kndCommit *commit, struct kndStateUpdate *update)
{
    if (!commit->update_tail) {
        commit->update_tail = update;
        commit->updates = update;
    } else {
        commit->update_tail->next = update;
        commit->update_tail = update;
    }
    commit->num_updates++;
}

int knd_commit_new(struct kndCommit **result, struct kndMemPool *mempool);
int knd_commit_ref_new(struct kndCommitRef **result, struct kndMemPool *mempool);

gsl_err_t knd_commit_process(void *obj, const char *rec, size_t *total_size);
int knd_commit_submit(struct kndCommit *commit, struct kndTask *task);
int knd_commit_resolve(struct kndCommit *commit, struct kndRepoSnapshot *snapshot, struct kndTask *task);
int knd_commit_dedup(struct kndCommit *commit, struct kndRepoSnapshot *snapshot, struct kndTask *task);

int knd_commit_calc_GSL_size(struct kndCommit *commit, size_t *result_size, struct kndTask *task);
int knd_commit_export_GSL(struct kndCommit *commit, struct kndOutput *out, size_t *total_size, struct kndTask *task);

int knd_commit_update_wal(struct kndCommit *commit, struct kndStorageWal *wal,
                          struct kndRepoSnapshot *snapshot,
                          size_t writer_id, struct kndTask *task);

int knd_commit_register(size_t rec_numid, const char *rec, size_t rec_size,
                        void *obj_ctx, size_t *result_size, void **result, struct kndTask *task);

int knd_commit_index(struct kndCommit *commit, struct kndStateLedger *ledger, struct kndTask *task);

/* collector */
int knd_collect_commits(struct kndRepoSnapshot *snapshot, size_t collector_id,
                        size_t *writer_ids, size_t num_writers, struct kndStateRange *range,
                        struct kndStateLedger *ledger, struct kndTask *task);

/* arbiter */
int knd_commit_detect_conflicts(struct kndRepoSnapshot *snapshot, struct kndTask **collectors, size_t num_collectors,
                                struct kndStateLedger *ledger, struct kndTask *task);

