#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h> // for SIZE_MAX
#include <time.h>

#include "knd_steward.h"
#include "knd_user.h"
#include "knd_task.h"
#include "knd_shared_dict.h"
#include "knd_set.h"
#include "knd_dict.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_storage.h"
#include "knd_output.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#define DEBUG_STEWARD_LEVEL_1 0
#define DEBUG_STEWARD_LEVEL_TMP 1

static int fetch_storage(struct kndSteward *steward, struct kndStorage **result)
{
    struct kndStorage *store;

    FOREACH (store, steward->storages) {
        switch (store->mode) {
        case KND_STORAGE_MODE_READ_WRITE:
            *result = store;
            return knd_OK;
        default:
            break;
        }
    }
    return knd_NO_MATCH;
}

static gsl_err_t parse_mem_main_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *self = obj;

    struct gslTaskSpec specs[] = {
        {   .name = "max-large-pages",
            .name_size = strlen("max-large-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_main_config.num_large_pages
        },
        {   .name = "max-base-pages",
            .name_size = strlen("max-base-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_main_config.num_base_pages
        },
        {   .name = "max-small-x4-pages",
            .name_size = strlen("max-small-x4-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_main_config.num_small_x4_pages
        },
        {   .name = "max-small-x2-pages",
            .name_size = strlen("max-small-x2-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_main_config.num_small_x2_pages
        },
        {   .name = "max-small-pages",
            .name_size = strlen("max-small-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_main_config.num_small_pages
        },
        {   .name = "max-tiny-pages",
            .name_size = strlen("max-tiny-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_main_config.num_tiny_pages
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_mem_task_ctx_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *self = obj;

    struct gslTaskSpec specs[] = {
        {   .name = "max-large-pages",
            .name_size = strlen("max-large-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_task_ctx_config.num_large_pages
        },
        {   .name = "max-base-pages",
            .name_size = strlen("max-base-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_task_ctx_config.num_base_pages
        },
        {   .name = "max-small-x4-pages",
            .name_size = strlen("max-small-x4-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_task_ctx_config.num_small_x4_pages
        },
        {   .name = "max-small-x2-pages",
            .name_size = strlen("max-small-x2-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_task_ctx_config.num_small_x2_pages
        },
        {   .name = "max-small-pages",
            .name_size = strlen("max-small-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_task_ctx_config.num_small_pages
        },
        {   .name = "max-tiny-pages",
            .name_size = strlen("max-tiny-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_task_ctx_config.num_tiny_pages
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_mem_task_cache_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *self = obj;

    struct gslTaskSpec specs[] = {
        {   .name = "max-large-pages",
            .name_size = strlen("max-large-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_task_cache_config.num_large_pages
        },
        {   .name = "max-base-pages",
            .name_size = strlen("max-base-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_task_cache_config.num_base_pages
        },
        {   .name = "max-small-x4-pages",
            .name_size = strlen("max-small-x4-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_task_cache_config.num_small_x4_pages
        },
        {   .name = "max-small-x2-pages",
            .name_size = strlen("max-small-x2-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_task_cache_config.num_small_x2_pages
        },
        {   .name = "max-small-pages",
            .name_size = strlen("max-small-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_task_cache_config.num_small_pages
        },
        {   .name = "max-tiny-pages",
            .name_size = strlen("max-tiny-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_task_cache_config.num_tiny_pages
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_mem_cache_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *self = obj;

    struct gslTaskSpec specs[] = {
        {   .name = "max-large-pages",
            .name_size = strlen("max-large-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_cache_config.num_large_pages
        },
        {   .name = "max-base-pages",
            .name_size = strlen("max-base-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_cache_config.num_base_pages
        },
        {   .name = "max-small-x4-pages",
            .name_size = strlen("max-small-x4-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_cache_config.num_small_x4_pages
        },
        {   .name = "max-small-x2-pages",
            .name_size = strlen("max-small-x2-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_cache_config.num_small_x2_pages
        },
        {   .name = "max-small-pages",
            .name_size = strlen("max-small-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_cache_config.num_small_pages
        },
        {   .name = "max-tiny-pages",
            .name_size = strlen("max-tiny-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_cache_config.num_tiny_pages
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_mem_user_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *self = obj;

    struct gslTaskSpec specs[] = {
        {   .name = "max-large-pages",
            .name_size = strlen("max-large-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_user_config.num_large_pages
        },
        {   .name = "max-base-pages",
            .name_size = strlen("max-base-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_user_config.num_base_pages
        },
        {   .name = "max-small-x4-pages",
            .name_size = strlen("max-small_x4-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_user_config.num_small_x4_pages
        },
        {   .name = "max-small-x2-pages",
            .name_size = strlen("max-small_x2-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_user_config.num_small_x2_pages
        },
        {   .name = "max-small-pages",
            .name_size = strlen("max-small-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_user_config.num_small_pages
        },
        {   .name = "max-tiny-pages",
            .name_size = strlen("max-tiny-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_user_config.num_tiny_pages
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t knd_parse_mem_main_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *self = obj;

    struct gslTaskSpec specs[] = {
       {   .name = "main",
           .name_size = strlen("main"),
           .parse = parse_mem_main_config,
           .obj = self
       },
       {   .name = "cache",
           .name_size = strlen("cache"),
           .parse = parse_mem_cache_config,
           .obj = self
       },
       {   .name = "user",
           .name_size = strlen("user"),
           .parse = parse_mem_user_config,
           .obj = self
       },
       {   .name = "task-ctx",
           .name_size = strlen("task-ctx"),
           .parse = parse_mem_task_ctx_config,
           .obj = self
       },
       {   .name = "task-cache",
           .name_size = strlen("task-cache"),
           .parse = parse_mem_task_cache_config,
           .obj = self
       }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_steward_role(void *obj, const char *name, size_t name_size)
{
    struct kndSteward *self = obj;

    if (name_size == strlen("Arbiter") && !memcmp(name, "Arbiter", name_size)) {
        self->role = KND_AGENT_ARBITER;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t reject_unrec_tag(void *unused_var(obj), const char *name, size_t name_size,
                                  const char *unused_var(rec), size_t *unused_var(total_size))
{
    knd_log("-- unrec tag \"%.*s\"", name_size, name);
    return make_gsl_err(gsl_FORMAT);
}

static gsl_err_t parse_base_repo(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *self = obj;

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

static gsl_err_t parse_user_settings(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *self = obj;

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

static gsl_err_t parse_schema_path(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *self = obj;

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

static gsl_err_t parse_steward_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *self = obj;
    int err;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .buf = self->name,
            .buf_size = &self->name_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {   .name = "role",
            .name_size = strlen("role"),
            .run = set_steward_role,
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
            .obj = obj,
        },
        {   .name = "init-data-path",
            .name_size = strlen("init-data-path"),
            .buf = self->data_path,
            .buf_size = &self->data_path_size,
            .max_buf_size = KND_PATH_SIZE
        },
        {   .name = "memory",
            .name_size = strlen("memory"),
            .parse = knd_parse_mem_main_config,
            .obj = obj,
        },
        {   .type = GSL_GET_ARRAY_STATE,
            .name = "storage",
            .name_size = strlen("storage"),
            .parse = knd_storage_parse_conf,
            .obj = obj
        },
        {   .validate = reject_unrec_tag,
            .obj = obj
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse {error %d} {tag %.*s}", parser_err.code,
                 parser_err.val_size, parser_err.val);
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

    err = fetch_storage(self, &self->active_storage);
    if (err) {
        knd_log("failed to fetch an active storage");
        return make_gsl_err(gsl_FAIL);
    }
    return make_gsl_err(gsl_OK);
}

static int steward_read_config(struct kndSteward *steward, const char *config, size_t config_size)
{
    struct gslTaskSpec specs[] = {
        {
            .name = "steward",
            .name_size = strlen("steward"),
            .parse = parse_steward_config,
            .obj = steward
        }
    };
    size_t total_parsed = config_size;
    gsl_err_t parser_err;
    struct kndOutput *out = steward->out;
    struct kndOutput *log = steward->log;

    parser_err = gsl_parse_task(config, &total_parsed, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code != gsl_OK) {
        knd_log("-- steward config failed");
        KND_STEWARD_LOG("failed to read configuration file");
        return gsl_err_to_knd_err_codes(parser_err);
    }
    return knd_OK;
}

#if 0
static int init_user_space(struct kndSteward *steward, struct kndTask *task)
{
    struct kndRepoAccess *acl;
    struct kndUser *user;
    int err;

    assert (steward->repo != NULL);

    if (!steward->user_class_name_size) {
        steward->user_class_name_size = strlen("User");
        memcpy(steward->user_class_name, "User", steward->user_class_name_size);
    }

    /* user manager */
    err = knd_user_new(&user, steward->user_class_name, steward->user_class_name_size,
                       steward->path, steward->path_size,
                       steward->user_repo_name, steward->user_repo_name_size,
                       steward->user_schema_path, steward->user_schema_path_size,
                       steward, task);
    KND_TASK_ERR("failed to create a user manager");

    err = knd_repo_access_new(&acl, task->mempool);
    KND_TASK_ERR("failed to alloc repo acl");

    acl->repo = steward->repo;
    acl->allow_read = true;
    acl->allow_write = true;
    task->user_ctx->acls = acl;    
    steward->user = user;

    task->user_ctx->mempool = steward->user->mempool_write;
    task->user_ctx->repo = steward->user->repo;
    task->user_ctx->acls = steward->user->default_acls;

    return knd_OK;
}
#endif

static int steward_init(struct kndSteward *steward)
{
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndOutput *out = steward->out;
    struct kndOutput *log = steward->log;
    int err;

    srand(time(NULL));

    err = knd_mkpath(steward->path, steward->path_size, 0755, false);
    KND_STEWARD_ERR("failed to make {steward-path %.*s}", steward->path_size, steward->path);

    /* system wide mempool types */
    steward->mem_main_config.memtype = KND_ALLOC_INCR;
    steward->mem_cache_config.memtype = KND_ALLOC_INCR;

    /* task local settings */
    steward->mem_task_ctx_config.memtype = KND_ALLOC_INCR;
    steward->mem_task_cache_config.memtype = KND_ALLOC_LIST;

    err = knd_task_new(&task, KND_AGENT_SYSTEM, 0,
                       &steward->mem_main_config, &steward->mem_cache_config,
                       steward);
    KND_STEWARD_ERR("failed to init steward main task");
    steward->task = task;

    err = knd_task_new(&steward->shift_task, KND_AGENT_SYSTEM, 0,
                       &steward->mem_main_config, &steward->mem_cache_config,
                       steward);
    KND_STEWARD_ERR("failed to init steward sync task");

    err = knd_set_new(&steward->repo_idx, KND_SET_STORE_MEMONLY, task->mempool);
    KND_STEWARD_ERR("failed to create a set idx");

    err = knd_dict_new(&steward->repo_name_idx, KND_SMALL_DICT_SIZE, KND_DICT_MEMONLY, task->mempool);
    KND_STEWARD_ERR("failed to create a repo name idx");

    err = knd_repo_new(&repo, "/", 1, steward->schema_path, steward->schema_path_size);
    KND_STEWARD_ERR("failed to create the root repo");
    steward->repo = repo;

    if (steward->data_path_size) {
        repo->data_path_size = steward->data_path_size;
        repo->data_path = steward->data_path;
    }

    err = knd_repo_read(repo, task);
    if (task->log->buf_size) {
        knd_log("ERR: %.*s", task->log->buf_size, task->log->buf);
    }
    KND_STEWARD_ERR("failed to open a repo");

    /* depends on {class User} from the system repo */
    /*err = init_user_space(steward, task);
    if (task->log->buf_size) {
        knd_log("ERR: %.*s", task->log->buf_size, task->log->buf);
    }
    KND_STEWARD_ERR("failed to init user space");
    */
    return knd_OK;
}

int knd_steward_new(struct kndSteward **result, const char *config, size_t config_size)
{
    struct kndSteward *steward;
    int err;

    steward = malloc(sizeof(struct kndSteward));
    if (!steward) return knd_NOMEM;
    memset(steward, 0, sizeof(struct kndSteward));

    err = knd_output_new(&steward->out, NULL, KND_TEMP_BUF_SIZE);
    if (err) return knd_NOMEM;
    err = knd_output_new(&steward->log, NULL, KND_TEMP_BUF_SIZE);
    if (err) return knd_NOMEM;
    
    err = steward_read_config(steward, config, config_size);
    if (err) goto error;

    err = steward_init(steward);
    if (err) goto error;

    *result = steward;
    return knd_OK;

 error:
    knd_log("%.*s", steward->msg_size, steward->msg);
    knd_steward_del(steward);
    return err;
}

void knd_steward_del(struct kndSteward *self)
{
    if (self->repo)
        knd_repo_del(self->repo);

    if (self->user)
        knd_user_del(self->user);

    if (self->task)
        knd_task_del(self->task);

    if (self->shift_task)
        knd_task_del(self->shift_task);

    free(self);
}

int knd_steward_snapshot_create(struct kndSteward *steward)
{
    struct kndOutput *out = steward->out;
    struct kndOutput *log = steward->log;
    struct kndTask *main_task = steward->task;
    struct kndTask *shift_task = steward->shift_task;
    struct kndRepo *repo = steward->repo;
    int err;

    shift_task->type = KND_TASK_BUILD_SNAPSHOT;

    err = knd_repo_build_snapshot(repo, main_task, shift_task);
    if (err) {
        log->write(log, shift_task->log->buf, shift_task->log->buf_size);
    }
    KND_STEWARD_ERR("failed to build a sys repo temp snapshot");

    err = knd_repo_read_cache(repo->snapshot_temp, shift_task);
    if (err) {
        log->write(log, shift_task->log->buf, shift_task->log->buf_size);
    }
    KND_STEWARD_ERR("failed to read a sys repo temp cache");

    return knd_OK;
}

int knd_steward_snapshot_activate(struct kndSteward *steward, struct kndRepoSnapshot **result)
{
    struct kndOutput *out = steward->out;
    struct kndOutput *log = steward->log;
    struct kndRepo *repo = steward->repo;
    struct kndTask *task = steward->shift_task;
    int err;

    if (DEBUG_STEWARD_LEVEL_TMP) {
        knd_log(".. activating new snapshot ..");
    }

    // TODO iterate all repos

    err = knd_repo_snapshot_activate(repo, result, steward->task, steward->shift_task);
    KND_STEWARD_ERR("failed to activate a snapshot repo");

    /* swap tasks */
    steward->shift_task = steward->task;
    steward->task = task;
    
    return knd_OK;
}
