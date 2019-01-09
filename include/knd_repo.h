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

    //struct kndStateRef  *curr_class_state_refs;
    //struct kndStateRef  *curr_class_inst_state_refs;

    struct kndStateRef  *class_state_refs;

    struct ooDict  *attr_name_idx;
    struct kndSet  *attr_idx;
    size_t num_attrs;

    struct ooDict  *class_inst_name_idx;

    struct kndProc *root_proc;
    struct ooDict  *proc_name_idx;
    struct kndSet  *proc_idx;
    size_t num_procs;
    size_t num_proc_insts;

    struct ooDict  *proc_arg_name_idx;
    struct kndSet  *proc_arg_idx;
    size_t num_proc_args;

    // remove
    //struct kndClassInst *curr_class_inst;
    struct kndAttrVar    *curr_attr_var;
    struct kndClassInst  *curr_inst;

    struct kndUpdate *updates;
    size_t max_updates;
    size_t num_updates;

    struct kndRepo *next;

    //void (*del)(struct kndRepo *self);
    //void (*str)(struct kndRepo *self);
};

int knd_present_repo_state(struct kndRepo *self,
                                  struct kndTask *task);
int knd_confirm_state(struct kndRepo *self, struct kndTask *task);

gsl_err_t knd_parse_repo(void *obj, const char *rec, size_t *total_size);

int knd_conc_folder_new(struct kndMemPool *mempool,
                        struct kndConcFolder **result);

int knd_repo_index_proc_arg(struct kndRepo *repo,
                            struct kndProc *self,
                            struct kndProcArg *arg,
                            struct kndTask *task);

void knd_repo_del(struct kndRepo *self);
int kndRepo_init(struct kndRepo *self, struct kndTask *task);
int kndRepo_new(struct kndRepo **self, struct kndMemPool *mempool);
