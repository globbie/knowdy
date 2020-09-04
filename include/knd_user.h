#pragma once

#include "knd_task.h"

struct kndRepo;
struct kndClassInst;
struct kndMemPool;
struct kndSet;

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
    struct kndClassInst *inst;
    struct kndRepo *repo;
    struct kndRepo *base_repo;

    struct kndRepoAccess *repo_acl;

    /* usage statistics */
    atomic_size_t num_workers;
    atomic_size_t total_tasks;

    struct kndUserContext *prev;
    struct kndUserContext *next;
};

struct kndUser
{
    char path[KND_PATH_SIZE];
    size_t path_size;

    const char *classname;
    size_t classname_size;
    struct kndClass *class;

    const char *reponame;
    size_t reponame_size;
    struct kndRepo *repo;

    const char *schema_path;
    size_t schema_path_size;

    struct kndMemPool *mempool;

    size_t max_users;
    size_t num_users;

    struct kndSet *user_idx;

    struct kndUserContext *top;
    struct kndUserContext *tail;
};

int knd_user_new(struct kndUser **self, const char *classname, size_t classname_size,
                 const char *path, size_t path_size, const char *reponame, size_t reponame_size,
                 const char *schema_path, size_t schema_path_size,
                 struct kndShard *shard, struct kndTask *task);
void knd_user_del(struct kndUser *self);

gsl_err_t knd_create_user(void *obj, const char *rec, size_t *total_size);
int knd_create_user_repo(struct kndTask *task);
gsl_err_t knd_parse_select_user(void *obj, const char *rec, size_t *total_size);

int knd_user_context_new(struct kndMemPool *mempool, struct kndUserContext **result);
