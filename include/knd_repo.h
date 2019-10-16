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

struct kndRepo
{
    char id[KND_ID_SIZE];
    size_t id_size;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char path[KND_PATH_SIZE];
    size_t path_size;

    /* array of ints => each task/writer can produce a number of WAL journals */
    size_t num_journals[KND_MAX_TASKS];
    time_t timestamp;

    const char *schema_name;
    size_t schema_name_size;

    const char *schema_path;
    size_t schema_path_size;

    struct kndUserContext *user_ctx;
    struct kndRepo *base;

    char **source_files;
    size_t num_source_files;

    const char *locale;
    size_t locale_size;

    /* local repo index */
    struct kndSharedDict *repo_idx;

    struct kndUser *user;
    size_t max_journal_size;

    bool restore_mode;
    size_t intersect_matrix_size;

    struct kndClass     *root_class;

    struct kndClassEntry *head_class_entry;
    struct kndClassEntry *tail_class_entry;

    struct kndSharedDict *class_name_idx;
    struct kndSet *class_idx;
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
    atomic_size_t num_proc_insts;
    atomic_size_t proc_inst_id_count;
    
    struct kndSharedDict *proc_arg_name_idx;
    struct kndSet  *proc_arg_idx;
    atomic_size_t   proc_arg_id_count;
    atomic_size_t   num_proc_args;

    /* commits */
    struct kndCommit * _Atomic commits;
    struct kndSet *commit_idx;
    atomic_size_t  num_commits;
    atomic_size_t  commit_id_count;
    size_t         max_commits;

    struct kndRepo *next;
};

int knd_present_repo_state(struct kndRepo *self,
                           struct kndTask *task);
int knd_confirm_commit(struct kndRepo *self, struct kndTask *task);

gsl_err_t knd_parse_repo(void *obj, const char *rec, size_t *total_size);

int knd_conc_folder_new(struct kndMemPool *mempool,
                        struct kndConcFolder **result);

int knd_repo_index_proc_arg(struct kndRepo *repo,
                            struct kndProc *self,
                            struct kndProcArg *arg,
                            struct kndTask *task);
int knd_repo_commit_indices(struct kndRepo *self,
                            struct kndTaskContext *ctx);
int knd_repo_check_conflicts(struct kndRepo *self,
                             struct kndTaskContext *ctx);
gsl_err_t knd_parse_repo_commit(void *obj,
                                const char *rec,
                                size_t *total_size);

int knd_repo_open(struct kndRepo *self, struct kndTask *task);

void knd_repo_del(struct kndRepo *self);

int knd_repo_new(struct kndRepo **self, struct kndMemPool *mempool);
