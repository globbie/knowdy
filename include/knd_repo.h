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
    char name[KND_PATH_SIZE];
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
    struct kndDict *repo_name_idx;

    struct kndClassEntry *cls_entries;
    struct kndClassEntry *cls_entries_tail;
    size_t num_cls_entries;
    size_t max_cls_entries;

    struct kndDict *cls_name_idx;
    char cls_name_idx_path[KND_PATH_SIZE + 1];
    size_t cls_name_idx_path_size;

    struct kndSet *cls_idx;
    char cls_idx_path[KND_PATH_SIZE + 1];
    size_t cls_idx_path_size;

    struct kndDict *attr_name_idx;
    char attr_name_idx_path[KND_PATH_SIZE + 1];
    size_t attr_name_idx_path_size;

    struct kndSet  *attr_idx;
    char attr_idx_path[KND_PATH_SIZE + 1];
    size_t attr_idx_path_size;

    struct kndSet  *str_idx;
    struct kndDict *str_dict;
    char str_idx_path[KND_PATH_SIZE + 1];
    size_t str_idx_path_size;
};

struct kndRepoIndices
{
    struct kndSharedDict *cls_name_idx;
    struct kndSharedSet *cls_idx;

    struct kndSharedDict *attr_name_idx;
    struct kndSharedSet  *attr_idx;
    atomic_size_t   attr_id_count;
    atomic_size_t   num_attrs;

    struct kndSharedSet  *proc_idx;
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
};

/*
 * frozen state repo snapshot
 * + live updates (commits)
 */
struct kndRepoSnapshot
{
    knd_snapshot_state state;
    knd_agent_role_type role;
    size_t numid;
    time_t timestamp;

    struct kndRepo *repo;
    char path[KND_PATH_SIZE + 1];
    size_t path_size;

    struct kndRepoCache cache;
    struct kndRepoIndices idxs;

    size_t start_from_commit_id;
    struct kndCommit * _Atomic commits;
    struct kndSet *commit_idx;
    atomic_size_t  num_commits;
    size_t         num_marshalled_commits;

    atomic_size_t  commit_id_count;
    size_t         max_commits;

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

    bool restore_mode;
    size_t intersect_matrix_size;

    struct kndRepoSnapshot *snapshot;
    struct kndRepoSnapshot *snapshot_temp;
};

int knd_present_repo_state(struct kndRepo *self, struct kndTask *task);
int knd_confirm_commit(struct kndRepo *self, struct kndTask *task);

gsl_err_t knd_parse_repo_select(void *obj, const char *rec, size_t *total_size);
int knd_repo_read_sources(struct kndRepo *self, struct kndTask *task);

int knd_repo_index_proc_arg(struct kndRepo *repo, struct kndProc *self,
                            struct kndProcArg *arg, struct kndTask *task);
int knd_repo_commit_indices(struct kndRepo *self, struct kndTaskContext *ctx);
int knd_repo_check_conflicts(struct kndRepo *self, struct kndTaskContext *ctx);
gsl_err_t knd_repo_parse_commit(void *obj, const char *rec, size_t *total_size);

int knd_apply_commit(void *elem, void *ctx, struct kndTask *task);

/* snapshots */
int knd_repo_snapshot_new(struct kndRepoSnapshot **result, size_t numid, size_t latest_commit_id,
                          struct kndRepo *repo, knd_agent_role_type role, struct kndTask *task);
int knd_snapshot_build_path(struct kndRepoSnapshot *s, struct kndTask *task);

int knd_repo_build_snapshot(struct kndRepo *self, struct kndTask *main_task, struct kndTask *task);
int knd_repo_snapshot_activate(struct kndRepo *repo, struct kndRepoSnapshot **result, struct kndTask *task);
int knd_repo_read(struct kndRepo *self, struct kndTask *task);
int knd_repo_restore(struct kndRepo *self, struct kndRepoSnapshot *snapshot, struct kndTask *task);
int knd_repo_update_cache(struct kndRepoSnapshot *snapshot, struct kndTask *task);

void knd_repo_snapshot_del(struct kndRepoSnapshot *snapshot);

int knd_repo_save_meta(struct kndRepoSnapshot *s, struct kndTask *task);

void knd_repo_del(struct kndRepo *self);



int knd_conc_folder_new(struct kndConcFolder **result, struct kndMemPool *mempool);

int knd_repo_transfer_commits(struct kndRepo *repo, struct kndTask *task);

int knd_repo_new(struct kndRepo **self, const char *name, size_t name_size,
                 const char *path, size_t path_size,
                 const char *schema_path, size_t schema_path_size);
