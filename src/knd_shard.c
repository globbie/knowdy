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
        {  .name = "memory",
            .name_size = strlen("memory"),
            .parse = parse_mem_config,
            .obj = self,
        },
        {   .name = "agent",
            .name_size = strlen("agent"),
            .parse = parse_agent,
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

static int parse_config(struct kndShard *self,
                        const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        {
            .name = "schema",
            .name_size = strlen("schema"),
            .parse = parse_schema,
            .obj = self
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code != gsl_OK) return gsl_err_to_knd_err_codes(parser_err);
    return knd_OK;
}

int knd_shard_new(struct kndShard **shard, const char *config, size_t config_size)
{
    struct kndShard *self;
    struct kndMemPool *mempool = NULL;
    struct kndRepo *repo;
    struct kndTask *task = NULL;
    struct kndRepoAccess *acl;
    struct kndUser *user;
    int err;

    self = malloc(sizeof(struct kndShard));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndShard));

    err = parse_config(self, config, &config_size);
    if (err != knd_OK) goto error;

    /* DB paths */
    err = knd_mkpath(self->path, self->path_size, 0755, false);
    if (err != knd_OK) {
        knd_log("-- failed to make path: \"%.*s\"", self->path_size, self->path);
        goto error;
    }

    err = knd_mempool_new(&mempool, KND_ALLOC_INCR, 0);
    if (err) goto error;
    mempool->num_pages = self->ctx_mem_config.num_pages;
    mempool->num_small_x4_pages = self->ctx_mem_config.num_small_x4_pages;
    mempool->num_small_x2_pages = self->ctx_mem_config.num_small_x2_pages;
    mempool->num_small_pages = self->ctx_mem_config.num_small_pages;
    mempool->num_tiny_pages = self->ctx_mem_config.num_tiny_pages;
    err = mempool->alloc(mempool);
    if (err) goto error;
    self->mempool = mempool;

    err = knd_set_new(mempool, &self->repo_idx);
    if (err) goto error;

    err = knd_shared_dict_new(&self->repo_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    /* system repo */
    err = knd_repo_new(&repo, "/", 1, self->schema_path, self->schema_path_size, mempool);
    if (err) goto error;
    self->repo = repo;

    err = knd_task_new(self, mempool, 0, &task);
    if (err) goto error;
    task->ctx = calloc(1, sizeof(struct kndTaskContext));
    if (!task->ctx) return knd_NOMEM;
    task->role = self->role;
    task->user_ctx->repo = repo;
    task->user_ctx->mempool = mempool;
    self->task = task;

    err = knd_repo_access_new(mempool, &acl);
    KND_TASK_ERR("failed to alloc repo acl");
    acl->repo = repo;
    acl->allow_read = true;
    acl->allow_write = true;
    task->user_ctx->acls = acl;
    
    err = knd_repo_open(repo, task);
    if (err != knd_OK) {
        knd_log("ERR LOG:%.*s", task->output_size, task->output);
        goto error;
    }

    if (!self->user_class_name_size) {
        self->user_class_name_size = strlen("User");
        memcpy(self->user_class_name, "User", self->user_class_name_size);
    }
    task->repo = repo;

    /* user manager */
    err = knd_user_new(&user, self->user_class_name, self->user_class_name_size,
                       self->path, self->path_size,
                       self->user_repo_name, self->user_repo_name_size,
                       self->user_schema_path, self->user_schema_path_size,
                       self, task);
    if (err) {
        knd_log("-- failed to create a user manager: %.*s", task->output_size, task->output);
        goto error;
    }
    self->user = user;

    /* clean up all temporary memblocks */
    if (task) knd_task_free_blocks(task);

    srand(time(NULL));

    *shard = self;
    return knd_OK;
 error:

    knd_shard_del(self);
    if (task) knd_task_free_blocks(task);
 
    return err;
}

void knd_shard_del(struct kndShard *self)
{
    if (self->repo)
        knd_repo_del(self->repo);

    if (self->task)
        knd_task_del(self->task);

    if (self->mempool)
        knd_mempool_del(self->mempool);

    if (self->user)
        knd_user_del(self->user);

    free(self);
}

