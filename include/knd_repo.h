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

#include <time.h>
#include <stdatomic.h>

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_task.h"

struct kndConcFolder
{
    const char *name;
    size_t name_size;
    struct kndConcFolder *parent;
    struct kndConcFolder *next;
};

/*  steady-state DB snapshot */
struct kndRepoSnapshot
{
    char path[KND_PATH_SIZE + 1];
    size_t path_size;

    knd_agent_role_type role;
    size_t numid;
    time_t timestamp;

    struct kndCommit * _Atomic commits;
    struct kndSet *commit_idx;
    atomic_size_t  num_commits;
    atomic_size_t  commit_id_count;
    size_t         max_commits;

    /* array of ints => each task/writer can produce a number of WAL journals */
    size_t num_journals[KND_MAX_TASKS];
    size_t max_journals;
    size_t max_journal_size;

    struct kndRepoSnapshot *prev;
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

    struct kndRepo *base;

    char **source_files;
    size_t num_source_files;

    const char *locale;
    size_t locale_size;

    bool restore_mode;
    size_t intersect_matrix_size;

    struct kndClass     *root_class;

    struct kndClassEntry *head_class_entry;
    struct kndClassEntry *tail_class_entry;

    struct kndSharedDict *class_name_idx;
    struct kndSharedSet *class_idx;
    atomic_size_t num_classes;
    atomic_size_t class_id_count;

    struct kndSharedDict *attr_name_idx;
    struct kndSet  *attr_idx;
    atomic_size_t   attr_id_count;
    atomic_size_t   num_attrs;

    struct kndProc *root_proc;
    struct kndSharedDict *proc_name_idx;
    struct kndSet  *proc_idx;
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

    struct kndRepoSnapshot * _Atomic snapshots;

    struct kndMemPool *mempool;
    struct kndMemBlock *blocks;
    size_t num_blocks;
    size_t total_block_size;
};

int knd_present_repo_state(struct kndRepo *self, struct kndTask *task);
int knd_confirm_commit(struct kndRepo *self, struct kndTask *task);
gsl_err_t knd_parse_repo(void *obj, const char *rec, size_t *total_size);

int knd_repo_index_proc_arg(struct kndRepo *repo, struct kndProc *self, struct kndProcArg *arg, struct kndTask *task);
int knd_repo_commit_indices(struct kndRepo *self, struct kndTaskContext *ctx);
int knd_repo_check_conflicts(struct kndRepo *self, struct kndTaskContext *ctx);
gsl_err_t knd_repo_parse_commit(void *obj, const char *rec, size_t *total_size);

int knd_repo_open(struct kndRepo *self, struct kndTask *task);
int knd_repo_restore(struct kndRepo *self, struct kndRepoSnapshot *snapshot, struct kndTask *task);
int knd_repo_snapshot(struct kndRepo *self, struct kndTask *task);

void knd_repo_del(struct kndRepo *self);

int knd_repo_snapshot_new(struct kndMemPool *mempool, struct kndRepoSnapshot **result);
int knd_conc_folder_new(struct kndMemPool *mempool, struct kndConcFolder **result);
int knd_repo_new(struct kndRepo **self, const char *name, size_t name_size,
                 const char *schema_path, size_t schema_path_size, struct kndMemPool *mempool);
