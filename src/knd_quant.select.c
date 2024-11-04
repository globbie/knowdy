#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_quant.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_set.h"
#include "knd_task.h"
#include "knd_utils.h"

#define DEBUG_QUANT_SELECT_LEVEL_0 0
#define DEBUG_QUANT_SELECT_LEVEL_1 0
#define DEBUG_QUANT_SELECT_LEVEL_2 0
#define DEBUG_QUANT_SELECT_LEVEL_3 0
#define DEBUG_QUANT_SELECT_LEVEL_TMP 1

struct LocalContext {
    struct kndQuantAttrStm *stm;
    struct kndTask    *task;
    struct kndRepo    *repo;
    struct kndQuantUInt *uint;
};

static gsl_err_t set_eq_val(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndQuantAttrStm *stm = ctx->stm;
    struct kndTask *task = ctx->task;
    struct kndQuantUInt *uint;
    int err;

    err = knd_quant_parse_uint(val, val_size, &uint, task);
    if (err) {
        KND_TASK_LOG("failed to parse uint value");
    }

    stm->uint = uint;
    knd_log(">> EQ {uint %zu}", uint->numval);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gt_val(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndQuantAttrStm *stm = ctx->stm;
    struct kndTask *task = ctx->task;
    struct kndQuantUIntRange *uint_range;
    struct kndQuantUInt *uint;
    int err;

    err = knd_quant_parse_uint(val, val_size, &uint, task);
    if (err) {
        KND_TASK_LOG("failed to parse uint value");
    }

    stm->uint = uint;
    knd_log(">> GT {uint %zu}", uint->numval);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gte_val(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndQuantAttrStm *stm = ctx->stm;
    struct kndTask *task = ctx->task;
    struct kndQuantUIntRange *uint_range;
    struct kndQuantUInt *uint;
    int err;
    
    err = knd_quant_parse_uint(val, val_size, &uint, task);
    if (err) {
        KND_TASK_LOG("failed to parse uint value");
    }

    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_lt_val(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndQuantAttrStm *stm = ctx->stm;
    struct kndTask *task = ctx->task;
    struct kndQuantUIntRange *uint_range;
    struct kndQuantUInt *uint;
    int err;

    err = knd_quant_parse_uint(val, val_size, &uint, task);
    if (err) {
        KND_TASK_LOG("failed to parse uint value");
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_lte_val(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndQuantAttrStm *stm = ctx->stm;
    struct kndTask *task = ctx->task;
    struct kndQuantUIntRange *uint_range;
    struct kndQuantUInt *uint;
    int err;
    
    err = knd_quant_parse_uint(val, val_size, &uint, task);
    if (err) {
        KND_TASK_LOG("failed to parse uint value");
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_approx_val(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndQuantAttrStm *stm = ctx->stm;
    struct kndTask *task = ctx->task;
    struct kndQuantUInt *uint;
    int err;

    err = knd_quant_parse_uint(val, val_size, &uint, task);
    if (err) {
        KND_TASK_LOG("failed to parse uint value");
    }
    stm->uint = uint;
    knd_log(">> approx val: %zu", uint->numval);

    return make_gsl_err(gsl_OK);
}

int knd_quant_uint_parse_stm(struct kndQuantAttrStm *stm,
                             const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_QUANT_SELECT_LEVEL_TMP) {
        knd_log(".. parse quant uint stm");
    }
    
    struct LocalContext ctx = {
        .task = task,
        .stm = stm
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_eq_val,
          .obj = &ctx
        },
        { .name = "gt",
          .name_size = strlen("gt"),
          .run = set_gt_val,
          .obj = &ctx
        },
        { .name = "gte",
          .name_size = strlen("gte"),
          .run = set_gte_val,
          .obj = &ctx
        },
        { .name = "lt",
          .name_size = strlen("gt"),
          .run = set_lt_val,
          .obj = &ctx
        },
        { .name = "lte",
          .name_size = strlen("gte"),
          .run = set_lte_val,
          .obj = &ctx
        },
        { .name = "approx",
          .name_size = strlen("approx"),
          .run = set_approx_val,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err.code;
    
    return knd_OK;
}
