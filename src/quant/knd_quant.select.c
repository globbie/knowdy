#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_quant.h"
#include "knd_facet.h"
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

static int elem_eq_lookup(struct kndAttrFacet *parent, struct kndQuantUInt *uint,
                          const char *seq, size_t seq_size,
                          struct kndSet **result, struct kndTask *task);
static int elem_gt_lookup(struct kndAttrFacet *parent, struct kndQuantUInt *uint,
                          const char *seq, size_t seq_size,
                          struct kndSet **result, struct kndTask *task);
static int elem_lt_lookup(struct kndAttrFacet *parent, struct kndQuantUInt *uint,
                          const char *seq, size_t seq_size,
                          struct kndSet **result, struct kndTask *task);

static gsl_err_t set_eq_val(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndQuantAttrStm *stm = ctx->stm;
    struct kndTask *task = ctx->task;
    struct kndQuantUInt *uint;
    int err;

    /* default type value:  KND_QUANT_EQ */

    err = knd_quant_parse_uint(val, val_size, &uint, task);
    if (err) {
        KND_TASK_LOG("failed to parse uint value");
        return make_gsl_err_external(err);
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
    struct kndQuantUIntRange *uint_range = stm->uint_range;
    struct kndQuantUInt *uint;
    int err;

    stm->type = KND_QUANT_RANGE;
    if (!uint_range) {
        err = knd_quant_uint_range_new(&uint_range, task->mempool);
        if (err) {
            KND_TASK_LOG("failed to alloc a uint range");
            return make_gsl_err_external(err);
        }
        stm->uint_range = uint_range;
    }

    err = knd_quant_parse_uint(val, val_size, &uint, task);
    if (err) {
        KND_TASK_LOG("failed to parse uint value");
        return make_gsl_err_external(err);
    }

    /* check range validity */
    if (uint_range->lt) {
        if (uint_range->lt->numval < uint->numval) {
            KND_TASK_LOG("invalid uint range given: LT limit (%zu) must be greater than GT (%zu)",
                         uint_range->lt->numval, uint->numval);
            return make_gsl_err_external(err);
        }
    }

    uint_range->gt = uint;
    knd_log(">> GT {uint %zu}", uint->numval);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gte_val(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndQuantAttrStm *stm = ctx->stm;
    struct kndTask *task = ctx->task;
   struct kndQuantUIntRange *uint_range = stm->uint_range;
    struct kndQuantUInt *uint;
    int err;

    stm->type = KND_QUANT_RANGE;
    if (!uint_range) {
        err = knd_quant_uint_range_new(&uint_range, task->mempool);
        if (err) {
            KND_TASK_LOG("failed to alloc a uint range");
            return make_gsl_err_external(err);
        }
        stm->uint_range = uint_range;
    }
   
    err = knd_quant_parse_uint(val, val_size, &uint, task);
    if (err) {
        KND_TASK_LOG("failed to parse uint value");
        return make_gsl_err_external(err);
    }
    /* check range validity */
    if (uint_range->lt) {
        if (uint_range->lt->numval < uint->numval) {
            KND_TASK_LOG("invalid uint range given: LT limit (%zu) must be greater than GT (%zu)",
                         uint_range->lt->numval, uint->numval);
            return make_gsl_err_external(err);
        }
    }

    uint_range->gt = uint;
    uint_range->gt_eq = true;

    knd_log(">> GTE {uint %zu}", uint->numval);
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_lt_val(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndQuantAttrStm *stm = ctx->stm;
    struct kndTask *task = ctx->task;
    struct kndQuantUIntRange *uint_range = stm->uint_range;
    struct kndQuantUInt *uint;
    int err;

    stm->type = KND_QUANT_RANGE;
    if (!uint_range) {
        err = knd_quant_uint_range_new(&uint_range, task->mempool);
        if (err) {
            KND_TASK_LOG("failed to alloc a uint range");
            return make_gsl_err_external(err);
        }
        stm->uint_range = uint_range;
    }

    err = knd_quant_parse_uint(val, val_size, &uint, task);
    if (err) {
        KND_TASK_LOG("failed to parse uint value");
        return make_gsl_err_external(err);
    }

    /* check range validity */
    if (uint_range->gt) {
        if (uint_range->gt->numval > uint->numval) {
            KND_TASK_LOG("invalid uint range given: GT limit (%zu) must be less than LT (%zu)",
                         uint_range->gt->numval, uint->numval);
            return make_gsl_err_external(err);
        }
    }

    uint_range->lt = uint;

    knd_log(">> LT {uint %zu}", uint->numval);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_lte_val(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndQuantAttrStm *stm = ctx->stm;
    struct kndTask *task = ctx->task;
    struct kndQuantUIntRange *uint_range = stm->uint_range;
    struct kndQuantUInt *uint;
    int err;

    stm->type = KND_QUANT_RANGE;
    if (!uint_range) {
        err = knd_quant_uint_range_new(&uint_range, task->mempool);
        if (err) {
            KND_TASK_LOG("failed to alloc a uint range");
            return make_gsl_err_external(err);
        }
        stm->uint_range = uint_range;
    }

    err = knd_quant_parse_uint(val, val_size, &uint, task);
    if (err) {
        KND_TASK_LOG("failed to parse uint value");
        return make_gsl_err_external(err);
    }
    /* check range validity */
    if (uint_range->gt) {
        if (uint_range->gt->numval > uint->numval) {
            KND_TASK_LOG("invalid uint range given: GT limit (%zu) must be less than LT (%zu)",
                         uint_range->gt->numval, uint->numval);
            return make_gsl_err_external(err);
        }
    }

    uint_range->lt = uint;
    uint_range->lt_eq = true;

    knd_log(">> LTE {uint %zu}", uint->numval);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_approx_val(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndQuantAttrStm *stm = ctx->stm;
    struct kndTask *task = ctx->task;
    struct kndQuantUInt *uint;
    int err;

    stm->type = KND_QUANT_APPROX;

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
    gsl_err_t parser_err;

    if (DEBUG_QUANT_SELECT_LEVEL_2) {
        knd_log(".. parsing quant uint stm");
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
        { .name = "eq",
          .name_size = strlen("eq"),
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

static int get_next_facet(struct kndAttrFacet *parent, const char *seq, size_t seq_size,
                          struct kndAttrFacet **result, struct kndTask *task)
{
    struct kndAttrFacet *f;
    int pos = 0;
    char c = '/';
    int err;

    assert (seq_size >= 1 && seq != NULL);

    switch (parent->type) {
    case KND_ATTR_FACET_SEQ_SIZE:
        pos = seq_size - 1;
        if (pos >= KND_MAX_FACETS) {
            err = knd_LIMIT;
            KND_TASK_ERR("uint seq limit exceeded");
        }
        break;
    case KND_ATTR_FACET_ACCUM:
        c = seq[seq_size - 1];
        pos = obj_id_base[(size_t)c];
        if (pos < 0) {
            err = knd_FORMAT;
            KND_TASK_ERR("invalid seq char");
        }
        seq_size--;
        break;
    default:
        err = knd_FORMAT;
        KND_TASK_ERR("unrecognized facet type %d", parent->type);
        break;
    }

    f = parent->children[pos];
    if (!f) {
        return knd_NO_MATCH;
    }

    *result = f;
    return knd_OK;
}

static int cache_eq_lookup(struct kndAttrFacet *facet, struct kndQuantUInt *uint,
                           struct kndSet **result, struct kndTask *task)
{
    struct kndAttrFacetElem *elem;
    struct kndQuantUInt *curr_uint;
    struct kndSet *set = NULL;
    struct kndQuantAttrStm *quant_attr_stm;
    int err;

    for (size_t i = 0; i < facet->num_elems; i++) {
        elem = facet->elems->cache[i];

        quant_attr_stm = elem->stm->subtype;
        curr_uint = quant_attr_stm->uint;

        if (DEBUG_QUANT_SELECT_LEVEL_3) {
            knd_log(".. matching {uint %zu} against {curr-uint %zu} {class %.*s}",
                    uint->numval, curr_uint->numval,
                    elem->entry->name_size, elem->entry->name);
        }

        if (curr_uint->numval != uint->numval) continue;

        if (!set) {
            err = knd_set_new(&set, task->mempool);
            KND_TASK_ERR("failed to alloc a result set");
        }

        err = knd_set_add(set, elem->entry->id, elem->entry->id_size, (void*)elem->entry);
        KND_TASK_ERR("failed to add an elem to result set");
    }

    if (!set) return knd_NO_MATCH;

    *result = set;
    return knd_OK;
}

static int elem_eq_lookup(struct kndAttrFacet *parent, struct kndQuantUInt *uint,
                          const char *seq, size_t seq_size,
                          struct kndSet **result, struct kndTask *task)
{
    struct kndAttrFacet *f;
    int err;

    err = get_next_facet(parent, seq, seq_size, &f, task);
    KND_TASK_ERR("failed to match a facet");

    if (f->num_elems <= KND_FACET_MAX_ELEM_CACHE) {
        err = cache_eq_lookup(f, uint, result, task);
        KND_TASK_ERR("failed to match a uint");
        return knd_OK;
    }

    if (seq_size) {
        err = elem_eq_lookup(f, uint, seq, seq_size, result, task);
        KND_TASK_ERR("failed to match a uint");
        return knd_OK;
    }

    // TODO short list -> set
    *result = f->elems->idx;

    return knd_OK;
}

/* 
 * GT lookup - find any value higher than uint
 */
static int elem_gt_lookup(struct kndAttrFacet *parent, struct kndQuantUInt *uint,
                          const char *seq, size_t seq_size,
                          struct kndSet **result, struct kndTask *task)
{
    struct kndAttrFacet *f;
    int err;

    if (DEBUG_QUANT_SELECT_LEVEL_TMP) {
        knd_log("RANGE GT lookup {uint %.*s}", uint->seq_size, uint->seq);
    }

    err = get_next_facet(parent, seq, seq_size, &f, task);
    KND_TASK_ERR("failed to match a facet");

    if (f->num_elems <= KND_FACET_MAX_ELEM_CACHE) {
        err = cache_eq_lookup(f, uint, result, task);
        KND_TASK_ERR("failed to match a uint");
        return knd_OK;
    }

    if (seq_size) {
        err = elem_gt_lookup(f, uint, seq, seq_size, result, task);
        KND_TASK_ERR("failed to match a uint");
        return knd_OK;
    }

    // TODO short list -> set
    *result = f->elems->idx;

    return knd_OK;
}

/* 
 * GT lookup - find any value less than uint
 */
static int elem_lt_lookup(struct kndAttrFacet *parent, struct kndQuantUInt *uint,
                          const char *seq, size_t seq_size,
                          struct kndSet **result, struct kndTask *task)
{
    struct kndAttrFacet *f;
    int err;

    err = get_next_facet(parent, seq, seq_size, &f, task);
    KND_TASK_ERR("failed to match a facet");

    if (f->num_elems <= KND_FACET_MAX_ELEM_CACHE) {
        err = cache_eq_lookup(f, uint, result, task);
        KND_TASK_ERR("failed to match a uint");
        return knd_OK;
    }

    if (seq_size) {
        err = elem_lt_lookup(f, uint, seq, seq_size, result, task);
        KND_TASK_ERR("failed to match a uint");
        return knd_OK;
    }

    // TODO short list -> set
    *result = f->elems->idx;

    return knd_OK;
}

int knd_quant_uint_query_plan(struct kndQuantAttrStm *stm, struct kndAttrFacet *facet,
                              struct kndTask *task)
{
    struct kndQuantUInt *uint;
    struct kndQuantUIntRange *uint_range;
    int err;

    /** calculating a total number of operations to complete the query

        if all indices are there and no intersections are needed
             num_potential_ops < MIN_OPS ?
        just return the result and consider the query completed
     **/

    if (DEBUG_QUANT_SELECT_LEVEL_2) {
        knd_log(".. planning an uint query");
        knd_attr_index_str(facet, "/", 1, 0);
    }

    switch (stm->type) {
    case KND_QUANT_EQ:
        uint = stm->uint;

        if (DEBUG_QUANT_SELECT_LEVEL_TMP) {
            knd_log("EQ lookup {uint %.*s}", uint->seq_size, uint->seq);
        }

        err = elem_eq_lookup(facet, uint, uint->seq, uint->seq_size, &stm->match, task);
        KND_TASK_ERR("failed to lookup an EQ value of uint %.*s", uint->seq_size, uint->seq);

        break;
    case KND_QUANT_RANGE:
        uint_range = stm->uint_range;

        assert (uint_range->gt != NULL || uint_range->lt != NULL);

        if (uint_range->gt) {
            uint = uint_range->gt;
            err = elem_gt_lookup(facet, uint, uint->seq, uint->seq_size, &stm->match, task);
            KND_TASK_ERR("failed to lookup a GT value of uint %.*s", uint->seq_size, uint->seq);
            break;
        }
        uint = uint_range->lt;
        err = elem_lt_lookup(facet, uint, uint->seq, uint->seq_size, &stm->match, task);
        KND_TASK_ERR("failed to lookup a LT value of uint %.*s", uint->seq_size, uint->seq);
        break;
    case KND_QUANT_APPROX:

        // TODO: auto determine GT and LT values

        break;
    default:
        break;
    }
    return knd_OK;
}
