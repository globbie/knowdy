#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include "knd_steward.h"
#include "knd_user.h"
#include "knd_task.h"
#include "knd_shared_dict.h"
#include "knd_set.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#define DEBUG_STEWARD_LEVEL_1 0
#define DEBUG_STEWARD_LEVEL_TMP 1

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

static gsl_err_t parse_mem_ctx_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *self = obj;

    struct gslTaskSpec specs[] = {
        {   .name = "max-large-pages",
            .name_size = strlen("max-large-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_ctx_config.num_large_pages
        },
        {   .name = "max-base-pages",
            .name_size = strlen("max-base-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_ctx_config.num_base_pages
        },
        {   .name = "max-small-x4-pages",
            .name_size = strlen("max-small-x4-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_ctx_config.num_small_x4_pages
        },
        {   .name = "max-small-x2-pages",
            .name_size = strlen("max-small-x2-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_ctx_config.num_small_x2_pages
        },
        {   .name = "max-small-pages",
            .name_size = strlen("max-small-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_ctx_config.num_small_pages
        },
        {   .name = "max-tiny-pages",
            .name_size = strlen("max-tiny-pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->mem_ctx_config.num_tiny_pages
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
       {   .name = "ctx",
           .name_size = strlen("ctx"),
           .parse = parse_mem_ctx_config,
           .obj = self
       }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_storage_quota_unit(void *obj, const char *name, size_t name_size)
{
    struct kndSteward *self = obj;

    for (size_t i = 0; i < sizeof knd_storage_unit_names / sizeof knd_storage_unit_names[0]; i++) {
        const char *unit_str = knd_storage_unit_names[i];
        assert(unit_str != NULL);

        size_t unit_str_size = strlen(unit_str);
        if (name_size != unit_str_size) continue;

        if (!memcmp(unit_str, name, name_size)) {
            self->storage_config.quota_unit = (knd_storage_unit_type)i;
            return make_gsl_err(gsl_OK);
        }
    }
    return make_gsl_err(gsl_FORMAT);
}

static gsl_err_t parse_storage_quota(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *self = obj;

    struct gslTaskSpec specs[] = {
        {   .name = "unit",
            .name_size = strlen("unit"),
            .run = set_storage_quota_unit,
            .obj = obj
        },
        {   .name = "total",
            .name_size = strlen("total"),
            .parse = gsl_parse_size_t,
            .obj = &self->storage_config.quota_total
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_storage_leaf_unit(void *obj, const char *name, size_t name_size)
{
    struct kndSteward *self = obj;

    for (size_t i = 0; i < sizeof knd_storage_unit_names / sizeof knd_storage_unit_names[0]; i++) {
        const char *unit_str = knd_storage_unit_names[i];
        assert(unit_str != NULL);

        size_t unit_str_size = strlen(unit_str);
        if (name_size != unit_str_size) continue;

        if (!memcmp(unit_str, name, name_size)) {
            self->storage_config.leaf_storage_unit = (knd_storage_unit_type)i;
            return make_gsl_err(gsl_OK);
        }
    }
    return make_gsl_err(gsl_FORMAT);
}

static gsl_err_t parse_storage_leaf(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *self = obj;

    struct gslTaskSpec specs[] = {
        {   .name = "unit",
            .name_size = strlen("unit"),
            .run = set_storage_leaf_unit,
            .obj = obj
        },
        {   .name = "min",
            .name_size = strlen("min"),
            .parse = gsl_parse_size_t,
            .obj = &self->storage_config.leaf_min_size
        },
        {   .name = "max",
            .name_size = strlen("max"),
            .parse = gsl_parse_size_t,
            .obj = &self->storage_config.leaf_max_size
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_storage_snapshot_threshold(void *obj, const char *val, size_t val_size)
{
    struct kndSteward *self = obj;
    char buf[KND_NUMFIELD_MAX_SIZE + 1] = { 0 };
    long double numval;
    int err;

    if (val_size > KND_NUMFIELD_MAX_SIZE) {
        knd_log("threshold value exceeds current num field limit");
        return make_gsl_err(gsl_FORMAT);
    }
    memcpy(buf, val, val_size);

    err = knd_parse_real(buf, &numval);
    if (err) return make_gsl_err(gsl_FORMAT);

    if (numval <= 0) return make_gsl_err(gsl_FORMAT);
    if (numval > 1) return make_gsl_err(gsl_FORMAT);

    self->storage_config.snapshot_threshold_ratio = numval;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_storage_snapshot(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        {   .name = "threshold",
            .name_size = strlen("threshold"),
            .run = set_storage_snapshot_threshold,
            .obj = obj
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_storage_config(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
       {   .name = "quota",
           .name_size = strlen("quota"),
           .parse = parse_storage_quota,
           .obj = obj
       },
       {   .name = "leaf",
           .name_size = strlen("leaf"),
           .parse = parse_storage_leaf,
           .obj = obj
       },
       {   .name = "snapshot",
           .name_size = strlen("snapshot"),
           .parse = parse_storage_snapshot,
           .obj = obj
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

static gsl_err_t set_steward_name(void *obj, const char *name, size_t name_size)
{
    struct kndSteward *self = obj;
    if (name_size >= KND_NAME_SIZE) {
        knd_log("steward name exceeds current limit");
        return make_gsl_err(gsl_FAIL);
    }
    memcpy(self->name, name, name_size);
    self->name_size = name_size;

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

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = set_steward_name,
            .obj = obj
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
        {   .name = "storage",
            .name_size = strlen("storage"),
            .parse = parse_storage_config,
            .obj = obj,
        },
        { .validate = reject_unrec_tag,
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

    err = knd_repo_access_new(task->mempool, &acl);
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

    /* system mempools for reading and writing */
    steward->mem_cache_config.memtype = KND_ALLOC_INCR;
    err = knd_mempool_create(&steward->mempool_read, &steward->mem_cache_config, 1);
    KND_STEWARD_ERR("failed to init a read-only system cache mempool");

    steward->mem_main_config.memtype = KND_ALLOC_INCR;
    err = knd_mempool_create(&steward->mempool_write, &steward->mem_main_config, 1);
    KND_STEWARD_ERR("failed to init a system mempool for writing");

    steward->mem_ctx_config.memtype = KND_ALLOC_INCR;

    err = knd_set_new(&steward->repo_idx, steward->mempool_write);
    KND_STEWARD_ERR("failed to create a set idx");

    err = knd_shared_dict_new(&steward->repo_name_idx, KND_MEDIUM_DICT_SIZE, steward->mempool_write, false);
    KND_STEWARD_ERR("failed to create a dict idx");

    err = knd_repo_new(&repo, "/", 1, steward->path, steward->path_size,
                       steward->schema_path, steward->schema_path_size);
    KND_STEWARD_ERR("failed to create the root repo");
    steward->repo = repo;

    if (steward->data_path_size) {
        repo->data_path_size = steward->data_path_size;
        repo->data_path = steward->data_path;
    }

    /* auxiliary service task */
    err = knd_task_new(&task, KND_AGENT_AUX, 0, steward);
    KND_STEWARD_ERR("failed to init a steward task");
    task->mempool = steward->mempool_write;
    task->cache_mempool = steward->mempool_read;
    steward->task = task;

    err = knd_repo_read(repo, task);
    knd_log("ERR: %.*s", task->log->buf_size, task->log->buf);
    KND_STEWARD_ERR("failed to open a repo");

    /* depends on {class User} from the system repo */
    err = init_user_space(steward, task);
    knd_log("ERR: %.*s", task->log->buf_size, task->log->buf);
    KND_STEWARD_ERR("failed to init user space");

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

    if (self->mempool_read)
        knd_mempool_del(self->mempool_read);
    if (self->mempool_write)
        knd_mempool_del(self->mempool_write);

    if (self->user)
        knd_user_del(self->user);

    if (self->task)
        knd_task_del(self->task);

    free(self);
}

void knd_steward_monitor(struct kndSteward *steward, struct kndResourceReport *report)
{
    struct kndMemPool *mempool = steward->mempool_write;
    struct kndMemPoolReport memrep = { 0 };

    knd_mempool_report(mempool, &memrep);

    report->mem_usage = memrep.total_mem_usage;
    report->mem_threshold_alert = memrep.mem_threshold_alert;
}

int knd_steward_snapshot_create(struct kndSteward *steward)
{
    struct kndOutput *out = steward->out;
    struct kndOutput *log = steward->log;
    struct kndTask *task = steward->task;
    struct kndRepo *repo;
    int err;

    err = knd_mempool_create(&steward->mempool_read_temp, &steward->mem_cache_config, 1);
    KND_STEWARD_ERR("failed to init a cache read-only mempool");
    steward->task->cache_mempool = steward->mempool_read_temp;

    err = knd_mempool_create(&steward->mempool_write_temp, &steward->mem_main_config, 1);
    KND_STEWARD_ERR("failed to init a write mempool");

    task->type = KND_BUILD_SNAPSHOT_STATE;
    task->mempool = steward->mempool_write_temp;
    repo = steward->repo;

    err = knd_repo_snapshot_create(repo, task);
    if (err) {
        log->write(log, task->log->buf, task->log->buf_size);
    }
    KND_STEWARD_ERR("failed to build a sys repo temp snapshot");

    task->snapshot = repo->snapshot_temp;
    task->idxs = &repo->snapshot_temp->idxs;
    task->mempool = steward->mempool_read_temp;

    err = knd_repo_snapshot_read(repo->snapshot_temp, task);
    if (err) {
        log->write(log, task->log->buf, task->log->buf_size);
    }
    KND_STEWARD_ERR("failed to read a sys repo temp snapshot");

    return knd_OK;
}

int knd_steward_snapshot_activate(struct kndSteward *steward)
{
    struct kndOutput *out = steward->out;
    struct kndOutput *log = steward->log;
    struct kndRepo *repo = steward->repo;
    int err;

    if (DEBUG_STEWARD_LEVEL_TMP)
        knd_log(".. activating new snapshot ..");

    // TODO iterate all repos

    err = knd_repo_snapshot_activate(repo, steward->task);
    KND_STEWARD_ERR("failed to activate a snapshot repo");

    // free prev mempools
    knd_mempool_del(steward->mempool_read);
    knd_mempool_del(steward->mempool_write);

    steward->mempool_read = steward->mempool_read_temp;
    steward->mempool_write = steward->mempool_write_temp;
 
    return knd_OK;
}
