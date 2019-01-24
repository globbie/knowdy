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

#include <time.h>
#include <stdatomic.h>

#include "knd_config.h"
#include "knd_mempool.h"

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
    size_t num_journals;
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
    struct ooDict *repo_idx;
    
    struct kndUser *user;
    size_t max_journal_size;

    struct kndStateControl *state_ctrl;
    bool restore_mode;
    size_t intersect_matrix_size;

    struct kndClass     *root_class;
    struct kndClassInst *root_inst;

    struct ooDict *class_name_idx;
    struct kndSet *class_idx;
    size_t num_classes;
    size_t num_class_insts;

    struct ooDict  *attr_name_idx;
    struct kndSet  *attr_idx;
    size_t num_attrs;
    struct ooDict  *class_inst_name_idx;

    struct kndProc *root_proc;
    struct ooDict  *proc_name_idx;
    struct kndSet  *proc_idx;
    struct ooDict  *proc_inst_name_idx;

    atomic_size_t num_procs;
    atomic_size_t num_proc_insts;
    
    struct ooDict  *proc_arg_name_idx;
    struct kndSet  *proc_arg_idx;
    size_t num_proc_args;

    struct kndSet *update_idx;
    atomic_size_t num_updates;
    size_t max_updates;

    atomic_size_t update_id_count;

    struct kndRepo *next;
};

int knd_present_repo_state(struct kndRepo *self,
                                  struct kndTask *task);
int knd_confirm_updates(struct kndRepo *self, struct kndTask *task);

gsl_err_t knd_parse_repo(void *obj, const char *rec, size_t *total_size);

int knd_conc_folder_new(struct kndMemPool *mempool,
                        struct kndConcFolder **result);

int knd_repo_index_proc_arg(struct kndRepo *repo,
                            struct kndProc *self,
                            struct kndProcArg *arg,
                            struct kndTask *task);

int knd_repo_open(struct kndRepo *self, struct kndTask *task);

void knd_repo_del(struct kndRepo *self);
int kndRepo_new(struct kndRepo **self, struct kndMemPool *mempool);
