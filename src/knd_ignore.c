#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_proc.h"
#include "knd_shard.h"
#include "knd_shared_set.h"
#include "knd_user.h"
#include "knd_utils.h"
#include "knd_ignore.h"
#include "knd_mempool.h"
#include "knd_output.h"

#define DEBUG_IGNORE_LEVEL_0 0
#define DEBUG_IGNORE_LEVEL_1 0
#define DEBUG_IGNORE_LEVEL_2 0
#define DEBUG_IGNORE_LEVEL_3 0
#define DEBUG_IGNORE_LEVEL_TMP 1

struct LocalContext {
    struct kndTask       *task;
    struct kndRepo       *repo;
    struct kndSituation  *sit;
    struct kndLogicClause  *clause;
};

gsl_err_t knd_ignore_value(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    int err;

    if (DEBUG_IGNORE_LEVEL_2)
        knd_log("== val: \"%.*s\"", val_size, val);

    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_ignore_obj(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    int err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = knd_ignore_value,
          .obj = obj
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = knd_ignore_list,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = knd_ignore_list,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .validate = knd_ignore_named_area,
          .obj = &ctx
        },
        { .validate = knd_ignore_named_area,
          .obj = ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        KND_TASK_LOG("list item import failed: %d", parser_err.code);
        return parser_err;
    }
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_ignore_list(void *obj, const char *name, size_t name_size,
                          const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    int err;

    if (DEBUG_IGNORE_LEVEL_2)
        knd_log(".. ignore list: \"%.*s\" REC: %.*s", name_size, name, 32, rec);

    struct LocalContext local_ctx = {
        .task = task
    };

    struct gslTaskSpec ignore_item_spec = {
        .is_list_item = true,
        .parse = knd_ignore_obj,
        .obj = &local_ctx
    };

    return gsl_parse_array(&ignore_item_spec, rec, total_size);
}

gsl_err_t knd_ignore_named_area(void *obj, const char *name, size_t name_size,
                                const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_IGNORE_LEVEL_2)
        knd_log(">> named area: \"%.*s\"", name_size, name);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = knd_ignore_value,
          .obj = obj
        },
        { .type = GSL_SET_STATE,
          .validate = knd_ignore_named_area,
          .obj = &ctx
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = knd_ignore_list,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = knd_ignore_list,
          .obj = &ctx
        },
        { .validate = knd_ignore_named_area,
          .obj = ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        KND_TASK_LOG("area parsing failed: %d", parser_err.code);
        return parser_err;
    }
    return make_gsl_err(gsl_OK);
}
