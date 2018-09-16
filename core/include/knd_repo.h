#pragma once

struct kndClass;
struct kndClassInst;
struct kndProc;
struct kndRel;
struct kndRepo;
struct kndUser;
struct kndUserContext;
struct kndQuery;
struct glbOutput;

#include "knd_config.h"

struct kndRepo
{
    char id[KND_ID_SIZE];
    size_t id_size;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char sid[KND_TID_SIZE];
    size_t sid_size;

    size_t state;
    char path[KND_PATH_SIZE];
    size_t path_size;

    size_t num_journals;

    char schema_path[KND_PATH_SIZE];
    size_t schema_path_size;

    struct kndUserContext *user_ctx;
    struct kndRepo *base;

    char **source_files;
    size_t num_source_files;

    const char *locale;
    size_t locale_size;

    struct glbOutput *out;
    struct glbOutput *dir_out;
    struct glbOutput *file_out;
    struct glbOutput *path_out;
    struct glbOutput *log;
    
    /* local repo index */
    struct ooDict *repo_idx;
    struct kndRepo *curr_repo;
    
    struct kndUser *user;
    struct kndTask *task;

    struct kndMemPool *mempool;
    size_t max_journal_size;

    struct kndStateControl *state_ctrl;

    bool restore_mode;
    size_t intersect_matrix_size;

    struct kndClass *root_class;

    struct ooDict *class_name_idx;
    struct kndSet *class_idx;
    size_t num_classes;

    struct ooDict *attr_name_idx;
    struct kndSet *attr_idx;
    size_t num_attrs;

    struct ooDict *class_inst_name_idx;

    struct kndProc *root_proc;
    struct kndRel *root_rel;

    
    struct kndClass *curr_class;
    
    /**********  interface methods  **********/
    void (*del)(struct kndRepo *self);
    void (*str)(struct kndRepo *self);
    int (*init)(struct kndRepo *self);
};

extern int kndRepo_init(struct kndRepo *self);
extern int kndRepo_new(struct kndRepo **self, struct kndMemPool *mempool);
