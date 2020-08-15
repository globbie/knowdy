#pragma once

#include <time.h>
#include "knd_config.h"

struct kndMemPool;
struct kndTask;

typedef enum knd_commit_confirm { KND_INIT_STATE, 
                                  KND_FAILED_STATE,
                                  KND_CONFLICT_STATE,
                                  KND_VALID_STATE,
                                  KND_PERSISTENT_STATE
} knd_commit_confirm;

struct kndCommit
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    size_t orig_state_id;
    char *rec;
    size_t rec_size;

    time_t timestamp;
    knd_commit_confirm _Atomic confirm;

    struct kndRepo *repo;

    struct kndStateRef *class_state_refs;
    size_t num_class_state_refs;

    struct kndStateRef *proc_state_refs;
    size_t num_proc_state_refs;

    bool is_restored;
    struct kndCommit *prev;
};

int knd_commit_new(struct kndMemPool *mempool,
                   struct kndCommit **result);
int knd_commit_mem(struct kndMemPool *mempool,
                   struct kndCommit **result);
int knd_resolve_commit(struct kndCommit *self, struct kndTask *task);
int knd_dedup_commit(struct kndCommit *commit, struct kndTask *task);

