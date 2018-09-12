#pragma once

#include "knd_utils.h"
#include "knd_task.h"
#include "knd_class.h"

struct kndObjEntry;
struct kndClassInst;
struct kndUser;
struct glbOutput;
struct kndRepo;
struct kndRepoSet;
struct kndStateManager;
struct kndMemPool;
struct kndSet;

typedef enum knd_user_role { KND_USER_ROLE_RETRIEVER, 
                             KND_USER_ROLE_LEARNER
                         } knd_user_role;

struct kndRepoAccess
{
    char repo_id[KND_ID_SIZE + 1];

    bool may_import;
    bool may_update;

    bool may_select;
    bool may_get;
};

struct kndUserContext
{
    struct kndClassInst *user_inst;
    struct kndRepo *repo;

    //TODO: ACL
};

struct kndUser
{
    char id[KND_ID_SIZE];
    char last_uid[KND_ID_SIZE];

    char name[KND_NAME_SIZE];
    size_t name_size;

    char db_state[KND_ID_SIZE];

    knd_user_role role;

    char path[KND_PATH_SIZE];
    size_t path_size;

    char sid[KND_TID_SIZE];
    size_t sid_size;

    knd_format format;
    size_t depth;
    size_t max_depth;

    char default_locale[KND_NAME_SIZE];
    size_t default_locale_size;

    const char *locale;
    size_t locale_size;

    struct kndStateControl *state_ctrl;
    size_t max_users;
    size_t num_users;


    struct kndShard *shard;
    struct kndTask *task;
    struct glbOutput *out;
    struct glbOutput *log;

    //void *update_service;

    /* user context storage */
    struct kndSet *user_idx;
    struct kndUserContext *curr_ctx;

    struct kndRepo *repo;
    struct kndMemPool *mempool;
   
    /**********  interface methods  **********/
    void (*del)(struct kndUser *self);
    void (*str)(struct kndUser *self);
    int (*init)(struct kndUser *self);

    gsl_err_t (*create)(struct kndUser *self,
                        const char *rec,
                        size_t *total_size);
    gsl_err_t (*select)(struct kndUser *self,
                        const char *rec,
                        size_t *total_size);

//    int (*get_user)(struct kndUser *self, const char *uid, struct kndUser **user);
//    int (*get_repo)(struct kndUser *self,
//                    const char *name, size_t name_size,
//                    struct kndRepo **result);

//    int (*restore)(struct kndUser *self);
//    int (*export)(struct kndUser *self);
//    int (*read)(struct kndUser *self, const char *rec);
};

extern int kndUser_new(struct kndUser **self, struct kndMemPool *mempool);

extern int knd_user_context_new(struct kndMemPool *mempool,
                                struct kndUserContext **result);
