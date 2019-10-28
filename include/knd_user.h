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
    struct kndClassInst *owner;
    char repo_id[KND_ID_SIZE];
    size_t repo_id_size;
    size_t repo_numid;

    bool may_import;
    bool may_update;

    bool may_select;
    bool may_get;
};

struct kndUserContext
{
    struct kndClassInst *user_inst;
    struct kndRepo *repo;

    //TODO: permits
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

    const char *class_name;
    size_t class_name_size;
    struct kndClass *class;

    const char *repo_name;
    size_t repo_name_size;
    const char *schema_path;
    size_t schema_path_size;

    //char sid[KND_TID_SIZE];
    //size_t sid_size;

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

    struct kndUserContext *curr_ctx;
    struct kndRepo *repo;
};

int knd_user_new(struct kndUser **self);
void knd_user_del(struct kndUser *self);

gsl_err_t knd_create_user(void *obj,
                          const char *rec,
                          size_t *total_size);
gsl_err_t knd_parse_select_user(void *obj,
                                const char *rec,
                                size_t *total_size);

int knd_user_context_new(struct kndMemPool *mempool,
                                struct kndUserContext **result);
