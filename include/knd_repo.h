#pragma once

struct kndClass;
struct kndClassInst;
struct kndProc;
struct kndProcArg;
struct kndRel;
struct kndRepo;
struct kndUser;
struct kndUserContext;
struct kndQuery;
struct kndTask;
struct kndSharedDict;
struct kndStorageLeaf;
struct kndRepoCache;
struct kndTaskContext;

#include <time.h>
#include <stdatomic.h>

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_task.h"

typedef enum knd_content_type {
    KND_GSL_DEFAULT,
    KND_GSL_CONFIG,
    KND_GSL_SCHEMA,
    KND_GSL_INIT_DATA
} knd_content_type;

typedef enum knd_snapshot_state {
    KND_SNAPSHOT_INIT,
    KND_SNAPSHOT_LOG,
    KND_SNAPSHOT_FULL
} knd_snapshot_state;

struct kndConcFolder
{
    const char *name;
    size_t name_size;

    struct kndConcFolder *parent;
    struct kndConcFolder *next;
};

struct kndRepoRef
{
    const char *name;
    size_t name_size;

    // sharding key

    struct kndRepo *repo;
    struct kndRepoRef *next;
};

struct kndRepoCache
{
    size_t snapshot_id;

    struct kndSet  *class_idx;
    struct kndDict *class_name_idx;

    struct kndSet  *proc_idx;
    struct kndDict *proc_name_idx;
};

struct kndRepoIndices
{
    struct kndSharedSet *class_idx;
    struct kndSharedDict *class_name_idx;
    atomic_size_t num_classes;
    atomic_size_t class_id_count;

    struct kndSet  *attr_idx;
    struct kndSharedDict *attr_name_idx;
    atomic_size_t   attr_id_count;
    atomic_size_t   num_attrs;

    struct kndSet  *proc_idx;
    struct kndSharedDict *proc_name_idx;
    struct kndSharedDict *proc_inst_name_idx;

    atomic_size_t num_procs;
    atomic_size_t proc_id_count;

    struct kndSharedDict *proc_arg_name_idx;
    struct kndSet  *proc_arg_idx;
    atomic_size_t   proc_arg_id_count;
    atomic_size_t   num_proc_args;

    struct kndSharedSet  *str_idx;
    struct kndSharedDict *str_dict;
    atomic_size_t num_strs;
};

/*  steady-state repo snapshot:
 *  - consists of N leaves (marshalled sets of objects)
 *  - reuses leaves from previous snapshots
 */
struct kndRepoSnapshot
{
    knd_snapshot_state state;
    knd_agent_role_type role;
    size_t numid;
    time_t timestamp;

    struct kndRepo *repo;

    struct kndRepoCache cache;
    struct kndRepoIndices idxs;

    size_t start_from_commit_id;
    struct kndCommit * _Atomic commits;
    struct kndSet *commit_idx;
    atomic_size_t  num_commits;
    size_t         num_marshalled_commits;

    atomic_size_t  commit_id_count;
    size_t         max_commits;

    size_t min_leaf_size;
    size_t max_leaf_size;

    struct kndStorageLeaf *class_db;
    struct kndStorageLeaf *class_inst_db;
    struct kndStorageLeaf *string_db;
    
    /* array of integers => each task/writer can produce a number of WAL journals */
    size_t num_journals[KND_MAX_TASKS];
    size_t max_journals;
    size_t max_journal_size;

    struct kndRepoSnapshot *prev;
    struct kndRepoSnapshot *next;
};

struct kndRepo
{
    char id[KND_ID_SIZE];
    size_t id_size;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char path[KND_PATH_SIZE];
    size_t path_size;

    const char *schema_name;
    size_t schema_name_size;
    const char *schema_path;
    size_t schema_path_size;

    const char *data_path;
    size_t data_path_size;

    struct kndRepo *base;
    struct kndRepo *parent;
    struct kndRepoRef *children;
    size_t num_children;

    char **source_files;
    size_t num_source_files;

    bool restore_mode;
    size_t intersect_matrix_size;

    struct kndClass *root_class;
    struct kndProc  *root_proc;

    struct kndRepoSnapshot * _Atomic snapshot;
    struct kndRepoSnapshot *snapshot_temp;

    struct kndMemPool *mempool;
    
    struct kndMemBlock *blocks;
    size_t num_blocks;
    size_t total_block_size;
};

int knd_present_repo_state(struct kndRepo *self, struct kndTask *task);
int knd_confirm_commit(struct kndRepo *self, struct kndTask *task);

gsl_err_t knd_parse_repo(void *obj, const char *rec, size_t *total_size);
int knd_repo_read_source_files(struct kndRepo *self, struct kndTask *task);

int knd_repo_index_proc_arg(struct kndRepo *repo, struct kndProc *self,
                            struct kndProcArg *arg, struct kndTask *task);
int knd_repo_commit_indices(struct kndRepo *self, struct kndTaskContext *ctx);
int knd_repo_check_conflicts(struct kndRepo *self, struct kndTaskContext *ctx);
gsl_err_t knd_repo_parse_commit(void *obj, const char *rec, size_t *total_size);
int knd_apply_commit(void *obj, const char *unused_var(elem_id), size_t unused_var(elem_id_size),
                     size_t unused_var(count), void *elem);

int knd_repo_open(struct kndRepo *self, struct kndTask *task);
int knd_repo_restore(struct kndRepo *self, struct kndRepoSnapshot *snapshot, struct kndTask *task);

int knd_repo_snapshot(struct kndRepo *self, size_t last_commit_id, struct kndTask *task);
void knd_repo_snapshot_free(struct kndRepoSnapshot *snapshot);
int knd_repo_cache_update(struct kndRepo *repo, struct kndTask *task);
int knd_repo_cleanup(struct kndRepo *repo, struct kndTask *task);

void knd_repo_del(struct kndRepo *self);

int knd_repo_snapshot_new(struct kndRepoSnapshot **result,
                          struct kndMemPool *cache_mempool, struct kndMemPool *mempool);
int knd_conc_folder_new(struct kndMemPool *mempool, struct kndConcFolder **result);

int knd_repo_transfer_commits(struct kndRepo *repo, struct kndTask *task);

int knd_repo_new(struct kndRepo **self, const char *name, size_t name_size,
                 const char *path, size_t path_size,
                 const char *schema_path, size_t schema_path_size, struct kndMemPool *mempool);
