#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_quant.h"
#include "knd_query.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_logic.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_SELECT_LEVEL_1 0
#define DEBUG_ATTR_SELECT_LEVEL_2 0
#define DEBUG_ATTR_SELECT_LEVEL_3 0
#define DEBUG_ATTR_SELECT_LEVEL_4 0
#define DEBUG_ATTR_SELECT_LEVEL_5 0
#define DEBUG_ATTR_SELECT_LEVEL_TMP 1

struct LocalContext {
    struct kndQuery   *query;
    struct kndClass   *class;
    struct kndTask    *task;
    struct kndRepo    *repo;

    struct kndAttrStm *clauses;
    struct kndAttrStm *attr_stm;
    struct kndAttr    *attr;
    knd_logic_t logic;
};

static gsl_err_t parse_nested_attr_stm(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size);

static gsl_err_t select_by_attr(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndClass *self = ctx->class;
    struct kndClassEntry *entry;
    struct kndClass *c;
    struct kndAttrStm *attr_stm;
    //struct kndAttrFacet *facet;
    struct kndOutput *log = task->log;
    struct kndAttr *attr = ctx->attr;
    int err;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    entry = attr->class_entry;
    err = knd_class_acquire(entry, &c, task);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_ATTR_SELECT_LEVEL_TMP) {
        knd_log("\n\n== _is class:%.*s  select %.*s attr (idx:%d) "
                " by value: \"%.*s\" (class: \"%.*s\" "
                " id:%.*s repo:%.*s)",
                self->name_size, self->name,
                attr->name_size, attr->name, attr->is_indexed,
                val_size, val,
                c->name_size, c->name,
                c->entry->id_size, c->entry->id,
                c->entry->repo->name_size, c->entry->repo->name);
    }

    /* TODO: special value: _null */
    if (val_size == strlen("_null")) {
        if (!memcmp(val, "_null", val_size)) {
            err = knd_attr_stm_new(&attr_stm, mempool);
            if (err) return make_gsl_err_external(err);
            attr_stm->attr = ctx->attr;

            attr_stm->next = ctx->clauses;
            ctx->clauses = attr_stm;
            return make_gsl_err(gsl_OK);
        }
    }
                
    err = knd_get_class(self->entry->repo, val, val_size, &c, task);
    if (err) {
        log->writef(log, "-- no such class: %.*s", val_size, val);
        task->http_code = HTTP_NOT_FOUND;
        return make_gsl_err_external(err);
    }
    
    if (task->num_sets + 1 > KND_MAX_CLAUSES)
        return make_gsl_err(gsl_LIMIT);

    //task->sets[task->num_sets] = facet->topics;
    //task->num_sets++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_classref_clause(struct kndAttr *attr,
                                       struct LocalContext *ctx,
                                       const char *rec, size_t *total_size)
{
    if (DEBUG_ATTR_SELECT_LEVEL_2)
        knd_log(".. parse classref attr \"%.*s\"..",
                attr->name_size, attr->name);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_attr,
          .obj = ctx
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_attr_stm_value(void *obj, const char *val, size_t val_size)
{
    struct kndAttrStm *self = obj;

    if (DEBUG_ATTR_SELECT_LEVEL_2)
        knd_log(".. set attr var value: %.*s %.*s",
                self->name_size, self->name, val_size, val);

    if (!val_size) return make_gsl_err(gsl_FORMAT);

    self->val = val;
    self->val_size = val_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr_stm(void *obj,
                                  const char *unused_var(name),
                                  size_t unused_var(name_size))
{
    struct kndAttrStm *attr_stm = obj;
    // TODO empty values?
    if (DEBUG_ATTR_SELECT_LEVEL_1) {
        if (!attr_stm->val_size)
            knd_log("NB: attr var value not set in %.*s",
                    attr_stm->name_size, attr_stm->name);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_spec_by_baseclass(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndClass *c;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_get_class(repo, name, name_size, &c, task);
    if (err) return make_gsl_err_external(err);

    /* TODO: check attr hubs */
    //ctx->class = c;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass_select(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct LocalContext *ctx = obj;
    gsl_err_t err;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP)
        knd_log(".. select spec by baseclass: \"%.*s\"..",
                64, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_spec_by_baseclass,
          .obj = ctx
        }/*,
        { .validate = parse_base_attr_select,
          .obj = ctx
          }*/
    };

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_nested_attr_stm(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrStm *self = ctx->attr_stm;
    struct kndTask    *task = ctx->task;
    struct kndAttrStm *attr_stm;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_attr_stm_new(&attr_stm, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_stm->parent = self;

    attr_stm->name = name;
    attr_stm->name_size = name_size;
    ctx->attr_stm = attr_stm;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP)
        knd_log(".. select nested attr: \"%.*s\" REC: %.*s",
                attr_stm->name_size, attr_stm->name, 64, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_stm_value,
          .obj = attr_stm
        },
        { .name = "_is",
          .name_size = strlen("_is"),
          .is_selector = true,
          .parse = parse_baseclass_select,
          .obj = ctx
        },
        { .validate = parse_nested_attr_stm,
          .obj = ctx
        },
        { .is_default = true,
          .run = confirm_attr_stm,
          .obj = attr_stm
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP)
        knd_log("++ attr var: \"%.*s\" val:%.*s",
                attr_stm->name_size, attr_stm->name,
                attr_stm->val_size, attr_stm->val);

    //attr_stm->next = self->children;
    //self->children = attr_stm;
    //self->num_children++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_inner_class_clause(struct kndAttr *attr,
                                          struct LocalContext *parent_ctx,
                                          const char *rec, size_t *total_size)
{
    gsl_err_t parser_err;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP)
        knd_log(".. parse inner class clause: \"%.*s\"..",
                attr->name_size, attr->name);

    struct LocalContext ctx = {
        .task = parent_ctx->task,
        .attr = parent_ctx->attr,
        .attr_stm = parent_ctx->attr_stm
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_attr,
          .obj = &ctx
        },
        { .validate = parse_nested_attr_stm,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (ctx.clauses) {
        ctx.clauses->next = parent_ctx->clauses;
        parent_ctx->clauses = ctx.clauses;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_str_clause(struct kndAttr *attr,
                                  struct LocalContext *ctx,
                                  const char *rec, size_t *total_size)
{
    if (DEBUG_ATTR_SELECT_LEVEL_2)
        knd_log(".. parse num attr \"%.*s\"..",
                attr->name_size, attr->name);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_attr,
          .obj = ctx
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

int knd_attr_parse_query_stm(struct kndAttrStm *stm,
                             const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndAttr *attr = stm->attr;
    struct kndQuantAttrStm *quant_attr_stm;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP) {
        knd_log(".. query by {attr %.*s}", attr->name_size, attr->name);
    }

    switch (attr->type) {
        /*case KND_ATTR_INNER:
        parser_err = parse_inner_class_clause(attr, &ctx, rec, total_size);
        if (parser_err.code) return parser_err.code;
        break;
    case KND_ATTR_REF:
        parser_err = parse_classref_clause(attr, &ctx, rec, total_size);
        if (parser_err.code) return parser_err.code;
        break;
        */
    case KND_ATTR_UINT:
        err = knd_quant_attr_stm_new(&quant_attr_stm, task->mempool);
        KND_TASK_ERR("failed to alloc a quant attr stm");
        stm->subtype = quant_attr_stm;

        err = knd_quant_uint_parse_stm(quant_attr_stm, rec, total_size, task);
        KND_TASK_ERR("failed to parse uint stm");
        break;
    default:
        knd_log("-- no clause filtering in attr %.*s",
                attr->name_size, attr->name);
        return knd_FAIL;
    }

    return knd_OK;
}
