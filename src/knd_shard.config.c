#include "knd_shard.h"

#include "knd_err.h"
#include "knd_mempool.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#include <string.h>
#include <stddef.h>

static gsl_err_t
parse_memory_settings(void *obj, const char *rec, size_t *total_size)
{
    struct kndMemPool *mempool = obj;
    return mempool->parse(mempool, rec, total_size);
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
            .max_buf_size = KND_NAME_SIZE
        },
        {   .name = "schema-path",
            .name_size = strlen("schema-path"),
            .parse = parse_schema_path,
            .obj = self,
        },
        {  .name = "memory",
            .name_size = strlen("memory"),
            .parse = parse_memory_settings,
            .obj = self->mempool,
        },
        {   .name = "num-workers",
            .name_size = strlen("num-workers"),
            .parse = gsl_parse_size_t,
            .obj = &self->num_workers
        },
        {   .name = "agent",
            .name_size = strlen("agent"),
            .buf = self->name,
            .buf_size = &self->name_size,
            .max_buf_size = KND_NAME_SIZE
        }
    };

    int err = knd_FAIL;
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return parser_err;
    }

    if (!self->path_size) {
        knd_log("-- DB path not set");
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_mkpath(self->path, self->path_size, 0755, false);
    if (err != knd_OK) return make_gsl_err_external(err);

    if (!self->schema_path_size) {
        knd_log("-- system schema path not set");
        return make_gsl_err(gsl_FAIL);
    }

    return make_gsl_err(gsl_OK);
}

int kndShard_parse_config(struct kndShard *self, const char *rec, size_t *total_size,
                          struct kndMemPool *mempool)
{
    self->mempool = mempool;
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
