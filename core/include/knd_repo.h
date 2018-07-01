#pragma once

struct kndClass;
struct kndProc;
struct kndRel;
struct kndObject;
struct kndRepo;
struct kndUser;
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

    char schema_path[KND_PATH_SIZE];
    size_t schema_path_size;

    char frozen_output_file_name[KND_PATH_SIZE];
    size_t frozen_output_file_name_size;

    const char *frozen_name_idx_path;
    size_t frozen_name_idx_path_size;

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

    struct kndRepoMigration *migrations[KND_MAX_MIGRATIONS];
    size_t num_migrations;
    struct kndRepoMigration *migration;
    
    struct kndRepoCache *cache;
    struct kndRepoCache *curr_cache;

    struct kndStateControl *state_ctrl;

    bool restore_mode;
    size_t intersect_matrix_size;

    struct kndClass *root_class;
    size_t next_class_numid;

    struct kndProc *root_proc;
    struct kndRel *root_rel;
    
    struct kndClass *curr_class;
    
    /**********  interface methods  **********/
    void (*del)(struct kndRepo *self);
    void (*str)(struct kndRepo *self);
    int (*init)(struct kndRepo *self);

    //int (*read_state)(struct kndRepo *self, const char *rec, size_t *chunk_size);
    gsl_err_t (*parse_task)(void *self, const char *rec, size_t *chunk_size);

    //int (*get_repo)(struct kndRepo *self, const char *uid, struct kndRepo **repo);

    int (*open)(struct kndRepo *self);
    int (*restore)(struct kndRepo *self);

    //int (*read)(struct kndRepo *self, const char *id);

    //int (*sync)(struct kndRepo *self);

    //int (*import)(struct kndRepo *self, const char *rec, size_t *total_size);
    //int (*update)(struct kndRepo *self, const char *rec);
    int (*export)(struct kndRepo *self, knd_format format);
};

extern int kndRepo_init(struct kndRepo *self);
extern int kndRepo_new(struct kndRepo **self, struct kndMemPool *mempool);
