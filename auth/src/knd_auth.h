#ifndef KND_AUTH_H
#define KND_AUTH_H

#define KND_NUM_AGENTS 1

#include <time.h>

#include "knd_dict.h"
#include "knd_utils.h"
#include "knd_user.h"

struct kndAuthRec
{
    char uid[KND_ID_SIZE];
    size_t num_requests;

    struct ooDict *cache;

    /* TODO: billing data */

    /* one time passwords */
    
};

struct kndUserRec
{
    char tid[KND_TID_SIZE];
    
};
    
struct kndResult
{
    knd_proc_state_t proc_state;
    knd_format format;

    char sid[KND_SID_SIZE];
    size_t sid_size;
    
    bool sid_required;
    
    char   *header;
    size_t header_size;

    char *obj;
    size_t obj_size;
    
    struct kndResult *next;
};

struct kndAuth
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    
    char db_host[KND_TEMP_BUF_SIZE];
    size_t db_host_size;
    
    char addr[KND_TEMP_BUF_SIZE];
    size_t addr_size;

    char inbox[KND_TEMP_BUF_SIZE];
    size_t inbox_size;

    char tid[KND_TID_SIZE];
    size_t tid_size;

    char sid[KND_SID_SIZE];
    size_t sid_size;

    struct ooDict *auth_idx;
    struct ooDict *sid_idx;
    struct ooDict *uid_idx;

    struct kndAuthRec *default_rec;
    struct kndAuthRec *spec_rec;
    
    struct ooDict *idx;

    struct kndUserRec *users;
    size_t max_users;
    size_t num_users;
    
    char *task;
    size_t task_size;
    char *obj;
    size_t obj_size;

    char *reply_obj;
    size_t reply_obj_size;

    struct kndUser *admin;
    struct kndOutput *out;
    
    /**********  interface methods  **********/
    int (*del)(struct kndAuth *self);
    void (*str)(struct kndAuth *self);

    int (*start)(struct kndAuth *self);
};

extern int kndAuth_new(struct kndAuth **self, 
			  const char *config);
#endif
