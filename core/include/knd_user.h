#ifndef KND_USER_H
#define KND_USER_H

#include "knd_utils.h"
#include "knd_task.h"
#include "knd_concept.h"

struct kndObject;
struct kndUser;
struct kndOutput;
struct kndRepo;
struct kndRepoSet;

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
    
struct kndUser
{
    char id[KND_ID_SIZE + 1];
    char last_uid[KND_ID_SIZE + 1];

    char name[KND_NAME_SIZE];
    size_t name_size;

    char db_state[KND_ID_SIZE + 1];
    
    knd_user_role role;

    const char *dbpath;
    size_t dbpath_size;
    
    char path[KND_TEMP_BUF_SIZE];
    size_t path_size;

    char sid[KND_NAME_SIZE];
    size_t sid_size;

    char default_locale[KND_NAME_SIZE];
    size_t default_locale_size;

    const char *locale;
    size_t locale_size;

    struct kndObject **user_idx;
    size_t max_users;
    size_t num_users;

    struct kndObject *curr_user;
    struct kndConcept *root_class;
    struct kndTask *task;
    struct kndOutput *out;
    struct kndOutput *log;
    
    void *update_service;
    
    struct kndRepo *repo;
    
    struct ooDict *class_idx;
    struct ooDict *browse_class_idx;
   
    /**********  interface methods  **********/
    int (*del)(struct kndUser *self);

    int (*str)(struct kndUser *self);

    int (*init)(struct kndUser *self);

    int (*run)(struct kndUser *self);

    int (*parse_task)(struct kndUser *self,
                      const char *rec,
                      size_t *total_size);

    int (*add_user)(struct kndUser *self);

    int (*get_user)(struct kndUser *self, const char *uid, struct kndUser **user);
    int (*get_repo)(struct kndUser *self,
                    const char *name, size_t name_size,
                    struct kndRepo **result);

    int (*restore)(struct kndUser *self);
    
    int (*import)(struct kndUser *self, char *rec, size_t *total_size);
    
    int (*read)(struct kndUser *self, const char *rec);
};


extern int kndUser_init(struct kndUser *self);
extern int kndUser_new(struct kndUser **self);
#endif
