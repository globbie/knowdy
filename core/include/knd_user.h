#ifndef KND_USER_H
#define KND_USER_H

#include "knd_utils.h"
#include "knd_policy.h"
#include "knd_dataclass.h"

struct kndObject;
struct kndUser;
struct kndOutput;
struct kndRepo;
struct kndRepoSet;
struct kndSpecInstruction;

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

    size_t num_users;

    const char *dbpath;
    size_t dbpath_size;
    
    char path[KND_TEMP_BUF_SIZE];
    size_t path_size;

    char sid[KND_TEMP_BUF_SIZE];
    size_t sid_size;

    char lang_code[KND_NAME_SIZE];
    size_t lang_code_size;

    char login_phrase[KND_TEMP_BUF_SIZE];
    size_t login_phrase_size;

    char control_phrase[KND_TEMP_BUF_SIZE];
    size_t control_phrase_size;

    struct kndDataClass *root_dc;
    struct kndSpecInstruction *instruct;
    struct kndOutput *out;
    
    void *update_inbox;
    
    struct kndRepo *repo;
    struct ooDict *repo_idx;

    struct ooDict *class_idx;
    struct ooDict *browse_class_idx;
    struct ooDict *user_idx;
   
    /**********  interface methods  **********/
    int (*del)(struct kndUser *self);

    int (*str)(struct kndUser *self);

    int (*init)(struct kndUser *self);

    int (*run)(struct kndUser *self);

    int (*add_user)(struct kndUser *self);

    int (*get_user)(struct kndUser *self, const char *uid, struct kndUser **user);

    int (*restore)(struct kndUser *self);
    
    int (*import)(struct kndUser *self, struct kndData *data);
    int (*update)(struct kndUser *self, struct kndData *data);

    int (*select)(struct kndUser *self, struct kndData *data);
    int (*update_select)(struct kndUser *self, struct kndData *data);

    int (*get_obj)(struct kndUser *self, struct kndData *data);
    int (*update_get_obj)(struct kndUser *self, struct kndData *data);

    int (*flatten)(struct kndUser *self, struct kndData *data);
    int (*update_flatten)(struct kndUser *self, struct kndData *data);

    int (*match)(struct kndUser *self, struct kndData *data);
    int (*update_match)(struct kndUser *self, struct kndData *data);

    int (*read)(struct kndUser *self, const char *rec);

    int (*sync)(struct kndUser *self);

};

extern int kndUser_init(struct kndUser *self);
extern int kndUser_new(struct kndUser **self);
#endif
