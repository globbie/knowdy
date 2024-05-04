#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include "knd_shard.h"
#include "knd_user.h"
#include "knd_task.h"
#include "knd_shared_dict.h"
#include "knd_set.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#define DEBUG_SHARD_LEVEL_1 0
#define DEBUG_SHARD_LEVEL_TMP 1

static gsl_err_t parse_ctx_mem_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndShard *self = obj;

    struct gslTaskSpec specs[] = {
        {   .name = "max_base_pages",
            .name_size = strlen("max_base_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->ctx_mem_config.num_pages
        },
        {   .name = "max_small_x4_pages",
            .name_size = strlen("max_small_x4_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->ctx_mem_config.num_small_x4_pages
        },
        {   .name = "max_small_x2_pages",
            .name_size = strlen("max_small_x2_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->ctx_mem_config.num_small_x2_pages
        },
        {   .name = "max_small_pages",
            .name_size = strlen("max_small_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->ctx_mem_config.num_small_pages
        },
        {   .name = "max_tiny_pages",
            .name_size = strlen("max_tiny_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->ctx_mem_config.num_tiny_pages
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_mem_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndShard *self = obj;

    struct gslTaskSpec specs[] = {
       {   .name = "ctx",
           .name_size = strlen("ctx"),
           .parse = parse_ctx_mem_config,
           .obj = self
        },
        {   .name = "max_base_pages",
            .name_size = strlen("max_base_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_config.num_pages
        },
        {   .name = "max_small_x4_pages",
            .name_size = strlen("max_small_x4_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_config.num_small_x4_pages
        },
        {   .name = "max_small_x2_pages",
            .name_size = strlen("max_small_x2_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_config.num_small_x2_pages
        },
        {   .name = "max_small_pages",
            .name_size = strlen("max_small_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_config.num_small_pages
        },
        {   .name = "max_tiny_pages",
            .name_size = strlen("max_tiny_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_config.num_tiny_pages
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t get_agent_role(void *obj, const char *name, size_t name_size)
{
    struct kndShard *self = obj;

    if (name_size == strlen("Arbiter") && !memcmp(name, "Arbiter", name_size)) {
        self->role = KND_ARBITER;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_agent(void *obj, const char *rec, size_t *total_size)
{
    struct kndShard *self = obj;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .buf = self->name,
            .buf_size = &self->name_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {   .name = "role",
            .name_size = strlen("role"),
            .run = get_agent_role,
            .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t
run_check_schema(void *unused_var(obj), const char *val, size_t val_size)
{
    const char *schema_name = "knd";
    size_t schema_name_size = strlen(schema_name);
    if (val_size != schema_name_size)  return make_gsl_err(gsl_FAIL);
    if (memcmp(schema_name, val, val_size)) return make_gsl_err(gsl_FAIL);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t reject_unrec_tag(void *unused_var(obj), const char *name, size_t name_size,
                                  const char *unused_var(rec), size_t *unused_var(total_size))
{
    knd_log("-- unrec tag \"%.*s\"", name_size, name);
    return make_gsl_err(gsl_FORMAT);
}

static gsl_err_t
parse_base_repo(void *obj, const char *rec, size_t *total_size)
{
    struct kndShard *self = obj;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .buf = self->user_repo_name,
            .buf_size = &self->user_repo_name_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {   .name = "schema-path",
            .name_size = strlen("schema-path"),
            .buf = self->user_schema_path,
            .buf_size = &self->user_schema_path_size,
            .max_buf_size = KND_PATH_SIZE
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t
parse_user_settings(void *obj, const char *rec, size_t *total_size)
{
    struct kndShard *self = obj;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .buf = self->user_class_name,
            .buf_size = &self->user_class_name_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {   .name = "base-repo",
            .name_size = strlen("base-repo"),
            .parse = parse_base_repo,
            .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t
parse_schema_path(void *obj, const char *rec, size_t *total_size)
{
    struct kndShard *self = obj;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .buf = self->schema_path,
            .buf_size = &self->schema_path_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {   .name = "user",
            .name_size = strlen("user"),
            .parse = parse_user_settings,
            .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t
parse_schema(void *obj, const char *rec, size_t *total_size)
{
    struct kndShard *self = obj;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = run_check_schema,
            .obj = self
        },
        {   .name = "db-path",
            .name_size = strlen("db-path"),
            .buf = self->path,
            .buf_size = &self->path_size,
            .max_buf_size = KND_PATH_SIZE
        },
        {   .name = "schema-path",
            .name_size = strlen("schema-path"),
            .parse = parse_schema_path,
            .obj = self,
        },
        {   .name = "init-data-path",
            .name_size = strlen("init-data-path"),
            .buf = self->data_path,
            .buf_size = &self->data_path_size,
            .max_buf_size = KND_PATH_SIZE
        },
        {  .name = "memory",
            .name_size = strlen("memory"),
            .parse = parse_mem_config,
            .obj = self,
        },
        {   .name = "agent",
            .name_size = strlen("agent"),
            .parse = parse_agent,
            .obj = self
        },
        { .validate = reject_unrec_tag,
          .obj = self
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return parser_err;
    }

    if (!self->path_size) {
        knd_log("DB path not set");
        return make_gsl_err(gsl_FAIL);
    }

    if (self->path[self->path_size - 1] != '/') {
        if (self->path_size + 1 >= KND_PATH_SIZE) {
            knd_log("DB path exceeds current limit");
            return make_gsl_err(gsl_FAIL);
        }
        self->path[self->path_size] = '/';
        self->path_size++;
        self->path[self->path_size] = '\0';
    }

    if (!self->schema_path_size) {
        knd_log("system schema path not set");
        return make_gsl_err(gsl_FAIL);
    }
    return make_gsl_err(gsl_OK);
}

int knd_shard_read_config(struct kndShard *shard, const char *config, size_t config_size)
{
    struct gslTaskSpec specs[] = {
        {
            .name = "schema",
            .name_size = strlen("schema"),
            .parse = parse_schema,
            .obj = shard
        }
    };
    size_t total_parsed = config_size;
    gsl_err_t parser_err;
    struct kndTask *task = shard->task;

    parser_err = gsl_parse_task(config, &total_parsed, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code != gsl_OK) {
        KND_SHARD_LOG("failed to read configuration file");
        return gsl_err_to_knd_err_codes(parser_err);
    }
    return knd_OK;
}

static int init_shard_user(struct kndShard *shard, struct kndTask *task)
{
    struct kndRepoAccess *acl;
    struct kndUser *user;
    int err;

    assert (shard->repo != NULL);

    if (!shard->user_class_name_size) {
        shard->user_class_name_size = strlen("User");
        memcpy(shard->user_class_name, "User", shard->user_class_name_size);
    }

    /* user manager */
    err = knd_user_new(&user, shard->user_class_name, shard->user_class_name_size,
                       shard->path, shard->path_size,
                       shard->user_repo_name, shard->user_repo_name_size,
                       shard->user_schema_path, shard->user_schema_path_size,
                       shard, task);
    KND_TASK_ERR("failed to create a user manager");

    err = knd_repo_access_new(task->mempool, &acl);
    KND_TASK_ERR("failed to alloc repo acl");
    acl->repo = shard->repo;
    acl->allow_read = true;
    acl->allow_write = true;
    task->user_ctx->acls = acl;    
    shard->user = user;

    task->user_ctx->mempool = shard->user->mempool;
    task->user_ctx->repo = shard->user->repo;
    task->user_ctx->acls = shard->user->default_acls;

    return knd_OK;
}

int knd_shard_init(struct kndShard *shard)
{
    struct kndTask *task = shard->task;
    struct kndMemPool *mempool;
    struct kndRepo *repo;
    int err;

    /* DB paths */
    err = knd_mkpath(shard->path, shard->path_size, 0755, false);
    KND_SHARD_ERR("failed to make {path %.*s}", shard->path_size, shard->path);

    err = knd_mempool_new(&mempool, KND_ALLOC_INCR, 0);
    KND_SHARD_ERR("failed to create a mempool");
    mempool->num_pages = shard->mem_config.num_pages;
    mempool->num_small_x4_pages = shard->mem_config.num_small_x4_pages;
    mempool->num_small_x2_pages = shard->mem_config.num_small_x2_pages;
    mempool->num_small_pages = shard->mem_config.num_small_pages;
    mempool->num_tiny_pages = shard->mem_config.num_tiny_pages;

    err = knd_mempool_alloc(mempool);
    KND_SHARD_ERR("failed to alloc a mempool");
    shard->mempool = mempool;

    err = knd_set_new(mempool, &shard->repo_idx);
    KND_SHARD_ERR("failed to create a set idx");

    err = knd_shared_dict_new(&shard->repo_name_idx, KND_MEDIUM_DICT_SIZE);
    KND_SHARD_ERR("failed to create a dict idx");

    /* system repo */
    err = knd_repo_new(&repo, "/", 1, shard->path, shard->path_size,
                       shard->schema_path, shard->schema_path_size, mempool);
    KND_SHARD_ERR("failed to create a repo");
    shard->repo = repo;

    if (shard->data_path_size) {
        repo->data_path_size = shard->data_path_size;
        repo->data_path = shard->data_path;
    }

    err = knd_task_init(task, shard, mempool);
    KND_SHARD_ERR("failed to init a shard task");

    err = knd_repo_open(repo, task);
    KND_SHARD_ERR("failed to open a repo");

    /* depends on class User from base repo */
    err = init_shard_user(shard, task);
    KND_SHARD_ERR("failed to init shard user");

    /* clean up all temporary memblocks */
    knd_task_free_blocks(task);

    srand(time(NULL));

    return knd_OK;
}

int knd_shard_new(const char *guid, size_t guid_size, struct kndShard **result)
{
    struct kndShard *shard;
    int err;

    shard = malloc(sizeof(struct kndShard));
    if (!shard) return knd_NOMEM;
    memset(shard, 0, sizeof(struct kndShard));
    shard->guid = guid;
    shard->guid_size = guid_size;

    err = knd_task_new(KND_ARBITER, 0, &shard->task);
    if (err) {
        knd_log("failed to alloc shard's auxiliary task");
        goto error;
    }

    *result = shard;
    return knd_OK;

 error:
    knd_shard_del(shard);
    return err;
}

void knd_shard_del(struct kndShard *self)
{
    if (self->repo)
        knd_repo_del(self->repo);

    if (self->mempool)
        knd_mempool_del(self->mempool);

    if (self->user)
        knd_user_del(self->user);

    if (self->task)
        knd_task_del(self->task);

    free(self);
}

