#pragma once

#include "knd_task.h"

struct kndRepo;
struct kndClassInst;
struct kndMemPool;
struct kndSet;

typedef enum knd_user_type {
    KND_USER_DEFAULT,
    KND_USER_AUTHENTICATED,
    KND_USER_REVIEWER,
    KND_USER_ADMIN
} knd_user_type;

struct kndRepoAccess
{
    struct kndRepo *repo;
    // char repo_id[KND_ID_SIZE];
    // size_t repo_id_size;
    size_t repo_numid;

    bool allow_read;
    bool allow_select;
    bool allow_write;
    bool allow_update;
};

struct kndUserContext
{
    knd_user_type type;
    char path[KND_PATH_SIZE + 1];
    size_t path_size;

    struct kndClassInst *inst;
    struct kndRepo *repo;
    struct kndRepo *base_repo;
    struct kndMemPool *mempool;

    struct kndRepoAccess *acls;

    /* usage statistics */
    atomic_size_t total_tasks;
    size_t cache_cell_num;
};

struct kndUser
{
    char path[KND_PATH_SIZE + 1];
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
    struct kndCache *cache;

    size_t max_users;
    size_t num_users;

    struct kndRepoAccess *default_acls;
    struct kndSet *user_idx;
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
int knd_repo_access_new(struct kndMemPool *mempool, struct kndRepoAccess **result);
