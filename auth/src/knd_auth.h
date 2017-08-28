#ifndef KND_AUTH_H
#define KND_AUTH_H

#include <time.h>

#include "knd_dict.h"
#include "knd_utils.h"
#include "knd_user.h"

#define KND_NUM_AGENTS 1
#define KND_MAX_TOKEN_CACHE 8
#define KND_MAX_TOKEN_SIZE 256
#define KND_MAX_SCOPE_SIZE 8

struct kndUserRec;

struct kndAuthTokenRec
{
    char tok[KND_MAX_TOKEN_SIZE];
    size_t tok_size;
    
    size_t expiry;

    char scope[KND_MAX_SCOPE_SIZE];
    size_t scope_size;

    struct kndUserRec *user;
    struct kndAuthTokenRec *prev;
    struct kndAuthTokenRec *next;
};

struct kndUserRec
{
    size_t id;
    char name[KND_NAME_SIZE];
    size_t name_size;
    
    struct kndAuthTokenRec token_storage[KND_MAX_TOKEN_CACHE];

    struct kndAuthTokenRec *tokens;
    struct kndAuthTokenRec *tail;
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

    struct ooDict *token_idx;

    struct kndUserRec **users;
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
    struct kndOutput *log;

    /**********  interface methods  **********/
    int (*del)(struct kndAuth *self);
    void (*str)(struct kndAuth *self);

    int (*start)(struct kndAuth *self);
    int (*update)(struct kndAuth *self);
};

extern int kndAuth_new(struct kndAuth **self, 
			  const char *config);
#endif
