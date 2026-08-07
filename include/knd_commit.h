#pragma once

#include <time.h>
#include "knd_config.h"

struct kndMemPool;
struct kndTask;
struct kndRepo;
struct kndStateRange;
struct kndRepoSnapshot;
struct kndStateLedger;

typedef enum knd_commit_phase_t { KND_INIT_STATE, 
                                  KND_FAILED_STATE,
                                  KND_CONFLICT_STATE,
                                  KND_VALID_STATE,
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
    time_t timestamp;

    size_t orig_state_id;
    char *rec;
    size_t rec_size;

    struct kndRepoSnapshot *snapshot;


    struct kndCommit *prev;
    struct kndCommit *next;
};

struct kndCommitRef {
    struct kndCommit *commit;
    struct kndCommit *next;
};

int knd_commit_new(struct kndCommit **result, struct kndMemPool *mempool);

gsl_err_t knd_commit_process(void *obj, const char *rec, size_t *total_size);
int knd_commit_confirm(struct kndCommit *commit, struct kndTask *task);
int knd_commit_resolve(struct kndCommit *commit, struct kndRepoSnapshot *snapshot, struct kndTask *task);
int knd_commit_dedup(struct kndCommit *commit, struct kndRepoSnapshot *snapshot, struct kndTask *task);

int knd_commit_calc_GSL_size(struct kndCommit *commit, size_t *result_size, struct kndTask *task);
int knd_commit_export_GSL(struct kndCommit *commit, size_t *total_size, struct kndTask *task);

int knd_commit_update_task_wal(struct kndCommit *commit, struct kndRepoSnapshot *snapshot,
                               size_t writer_id, struct kndTask *task);

int knd_commit_detect_conflicts(struct kndRepoSnapshot *snapshot, struct kndTask **collectors, size_t num_collectors,
                                struct kndStateLedger *ledger, struct kndTask *task);

int knd_commit_collect(struct kndRepoSnapshot *snapshot, 
                       size_t *writer_ids, size_t num_writers, struct kndStateRange *range,
                       struct kndStateLedger *ledger, struct kndTask *task);

int knd_commit_index(struct kndCommit *commit, struct kndStateLedger *ledger, struct kndTask *task);
