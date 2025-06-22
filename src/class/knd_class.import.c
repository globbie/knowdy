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
#include "knd_dict.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_quant.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_logic.h"
#include "knd_utils.h"
#include "knd_ignore.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_CLASS_IMPORT_LEVEL_1 0
#define DEBUG_CLASS_IMPORT_LEVEL_2 0
#define DEBUG_CLASS_IMPORT_LEVEL_3 0
#define DEBUG_CLASS_IMPORT_LEVEL_4 0
#define DEBUG_CLASS_IMPORT_LEVEL_5 0
#define DEBUG_CLASS_IMPORT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndClass *class;
    struct kndClassBasePred *base_pred;
};

static int update_class_name_idx(struct kndRepo *repo, struct kndClass *c,
                                 const char *name, size_t name_size, struct kndTask *task)
{
    struct kndCharSeq *seq;
    struct kndClassEntry *entry;
    int err;

    err = knd_class_entry_new(&entry, task->mempool);
    KND_TASK_ERR("failed to alloc a class entry");
    entry->repo = repo;
    entry->cached_version = c;
    c->entry = entry;

    entry->name = name;
    entry->name_size = name_size;
    c->name = name;
    c->name_size = name_size;

    /* register as a unique class name */
    err = knd_shared_dict_set(task->idxs->class_name_idx, name, name_size, (void*)entry);
    KND_TASK_ERR("failed to register a class name");

    /* class name as a charseq */
    err = knd_charseq_fetch(repo, name, name_size, &seq, task);
    KND_TASK_ERR("failed to encode a class name {seq %.*s}", name_size, name);
    entry->seq = seq;

    return knd_OK;
}

static gsl_err_t set_class_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *c = ctx->class;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndClassEntry *entry;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log("set {class %.*s} {num-strs %zu}", name_size, name, task->idxs->num_strs);
    }
    assert(repo != NULL);

    /* task mode: initial bulk load */
    switch (task->type) {
    case KND_TASK_BULK_LOAD:
        knd_build_conc_abbr(name, name_size, c->abbr, &c->abbr_size);

        entry = knd_shared_dict_get(task->idxs->class_name_idx, name, name_size);
        if (entry) {
            KND_TASK_LOG("{class %.*s} already exists", name_size, name);
            err = KND_CONFLICT;
            return make_gsl_err_external(err);
        }

        err = update_class_name_idx(repo, c, name, name_size, task);
        if (err) {
            KND_TASK_LOG("failed to update class name idx with {class %.*s}", name_size, name);
            return make_gsl_err_external(err);
        }
        return make_gsl_err(gsl_OK);
    default:
        break;
    }

    /* commit in progress */
    err = knd_get_class_by_name(repo, name, name_size, &c, task);
    if (!err) {
        KND_TASK_LOG("{class %.*s} already exists in {repo %.*s}", name_size, name);
        task->ctx->http_code = HTTP_CONFLICT;
        task->ctx->error = KND_CONFLICT;
        return make_gsl_err(gsl_FAIL);
    }

    /* check user shared repo */
    if (task->user_ctx->base_repo) {
        err = knd_get_class_by_name(task->user_ctx->base_repo, name, name_size, &c, task);
        if (!err) {
            KND_TASK_LOG("\"%.*s\" class already exists in a base repo: %.*s",
                         name_size, name,
                         task->user_ctx->base_repo->name_size,
                         task->user_ctx->base_repo->name);
            task->ctx->http_code = HTTP_CONFLICT;
            task->ctx->error = KND_CONFLICT;
            return make_gsl_err(gsl_FAIL);
        }
    }

    /* update local task idx */
    entry = knd_dict_get(task->class_name_idx, name, name_size);
    if (!entry) {

        /*entry->name = name;
        entry->name_size = name_size;
        self->name = name;
        self->name_size = name_size;
        err = knd_dict_set(task->class_name_idx, name, name_size, (void*)entry);
        if (err) return make_gsl_err_external(err);
        */
        return make_gsl_err(gsl_OK);
    }

    KND_TASK_LOG("current commit already has a doublet of {class %.*s}", name_size, name);
    task->ctx->error = KND_CONFLICT;
    return make_gsl_err(gsl_FAIL);
}

static gsl_err_t set_base_pred(void *obj, const char *name, size_t name_size)
{
    struct kndClassBasePred *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_logic_clause(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndLogicClause *clause;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log(".. parsing logic clause: \"%.*s\"", 32, rec);
    }
    err = knd_logic_clause_new(&clause, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = knd_logic_clause_parse(clause, rec, total_size, task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr(void *obj, const char *name, size_t name_size,
                            const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;
    struct kndTask *task = ctx->task;
    struct kndAttr *attr;
    struct kndQuantAttr *quant_attr;
    struct kndClassRefAttr *cls_ref_attr;
    struct kndClassInnerAttr *cls_inner_attr;
    struct kndMemPool *mempool = task->mempool;
    struct kndText *tr = task->ctx->tr;
    size_t num_attr_types = sizeof(knd_attr_names) / sizeof(knd_attr_names[0]);
    const char *c;
    int err;
    gsl_err_t parser_err;

    task->ctx->tr = NULL;

    if (DEBUG_CLASS_IMPORT_LEVEL_3) {
        knd_log(".. parsing {attr %.*s} rec:\"%.*s\"", name_size, name, 32, rec);
    }
    err = knd_attr_new(&attr, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->owner = self;

    for (size_t i = 0; i < num_attr_types; i++) {
        c = knd_attr_names[i];
        if (name_size != strlen(c)) continue;
        if (!memcmp(c, name, name_size)) {
            attr->type = (knd_attr_type)i;
            break;
        }
    }

    switch (attr->type) {
    case KND_ATTR_NONE:
        knd_log("{attr-type %.*s} is not supported for {class %.*s}",
                name_size, name, self->name_size, self->name);
        return make_gsl_err_external(knd_NO_MATCH);
    case KND_ATTR_UINT:
        err = knd_quant_attr_new(&quant_attr, KND_QUANT_UINT, name, name_size, task->mempool);
        if (err) {
            return make_gsl_err_external(err);
        }
        attr->subtype = quant_attr;
        break;
    case KND_ATTR_UREAL:
        err = knd_quant_attr_new(&quant_attr, KND_QUANT_UREAL, name, name_size, task->mempool);
        if (err) {
            return make_gsl_err_external(err);
        }
        attr->subtype = quant_attr;
        break;
    case KND_ATTR_CLS_REF:
        err = knd_cls_ref_attr_new(&cls_ref_attr, name, name_size, task->mempool);
        if (err) {
            return make_gsl_err_external(err);
        }
        attr->subtype = cls_ref_attr;
        break;
    case KND_ATTR_CLS_INNER:
        err = knd_cls_inner_attr_new(&cls_inner_attr, name, name_size, task->mempool);
        if (err) {
            return make_gsl_err_external(err);
        }
        attr->subtype = cls_inner_attr;
        break;
        /*case KND_ATTR_REL:
        parser_err = knd_rel_import(attr, task, rec, total_size);
        if (parser_err.code) {
            if (DEBUG_CLASS_IMPORT_LEVEL_3)
                knd_log("-- failed to parse the rel field: %d", parser_err.code);
            return parser_err;
        }
        return make_gsl_err(gsl_OK);*/
    default:
        break;
    }

    parser_err = knd_attr_import(attr, task, rec, total_size);
    if (parser_err.code) {
        return parser_err;
    }
 
    knd_class_append_attr(self, attr);

    /* restore owner's glosses */
    task->ctx->tr = tr;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_stm(void *obj, const char *name, size_t name_size,
                                 const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClassBasePred *bp = ctx->base_pred;
    struct kndAttrStm *stm;
    int err;

    err = knd_attr_stm_new(&stm, bp->subj, task->mempool);
    if (err) {
        return *total_size = 0, make_gsl_err_external(err);
    }
    stm->name = name;
    stm->name_size = name_size;

    err = knd_import_attr_stm(stm, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    knd_base_pred_append_attr_stm(bp, stm);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_stm_list(void *obj, const char *name, size_t name_size,
                                      const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClassBasePred *bp = ctx->base_pred;
    struct kndAttrStm *stm;
    int err;

    err = knd_attr_stm_new(&stm, bp->subj, task->mempool);
    if (err) {
        return *total_size = 0, make_gsl_err_external(err);
    }
    stm->name = name;
    stm->name_size = name_size;

    err = knd_import_attr_stm_list(stm, name, name_size,
                                   rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    assert (stm->list != NULL);

    knd_base_pred_append_attr_stm(bp, stm);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_base_pred(const char *rec, size_t *total_size, struct LocalContext *ctx)
{
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_base_pred,
          .obj = ctx->base_pred
        },
        { .validate = import_attr_stm,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = import_attr_stm_list,
          .obj = ctx
        },
        { .name = "_pred",
          .name_size = strlen("_pred"),
          .parse = parse_logic_clause,
          .obj = ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClass *cls = ctx->class;
    struct kndClassBasePred *base_pred;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log(".. parsing the base {class %.*s}", 32, rec);
    }
    err = knd_class_base_pred_new(&base_pred, cls, mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc a base pred");
        return *total_size = 0, make_gsl_err_external(err);
    }
    ctx->base_pred = base_pred;

    parser_err = parse_base_pred(rec, total_size, ctx);
    if (parser_err.code) return parser_err;

    knd_class_append_base_pred(cls, base_pred);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_uniq_type(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. set uniq type: \"%.*s\" for \"%.*s\"",
                name_size, name, self->name_size, self->name);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t add_uniq_attr(void *obj, const char *name, size_t name_size,
                               const char *unused_var(rec), size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndAttrRef *ref;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(">> add uniq attr: \"%.*s\"", name_size, name);

    err = knd_attr_ref_new(&ref, mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc kndAttrRef");
        return *total_size = 0, make_gsl_err_external(err);
    }
    ref->name = name;
    ref->name_size = name_size;

    ref->next = self->uniq;
    self->uniq = ref;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_uniq_attr_constraint(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_uniq_type,
          .obj = ctx
        },
        { .validate = add_uniq_attr,
          .obj = ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_class_import(struct kndRepo *repo, const char *rec, size_t *total_size,
                           struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndClass *c;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log(".. {worker %zu} to import {cls %.*s}", task->id, 128, rec);
    }
    err = knd_class_new(&c, mempool);
    if (err) {
        KND_TASK_LOG("mempool failed to alloc a class");
        return make_gsl_err_external(err);
    }

    struct LocalContext ctx = {
        .task = task,
        .repo = repo,
        .class = c
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_name,
          .obj = &ctx
        },
        { .name = "is",
          .name_size = strlen("is"),
          .parse = parse_baseclass,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "gloss",
          .name_size = strlen("gloss"),
          .parse = knd_parse_gloss_array,
          .obj = task
        },
        { .name = "uniq",
          .name_size = strlen("uniq"),
          .parse = parse_uniq_attr_constraint,
          .obj = &ctx
        },
        { .name = "clause",
          .name_size = strlen("clause"),
          .parse = knd_ignore_obj,
          .obj = &ctx
        },
        { .validate = parse_attr,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        switch (parser_err.code) {
        case gsl_NO_MATCH:
            KND_TASK_LOG("unrecognized tag \"%.*s\" in {cls %.*s}",
                         parser_err.val_size, parser_err.val,
                         c->name_size, c->name);
            break;
        default:
            KND_TASK_LOG("{cls %.*s} parsing error: %d",
                         c->name_size, c->name, parser_err.code);
            break;
        }
        goto final;
    }

    if (!c->name_size) {
        KND_TASK_LOG("no class name specified");
        task->http_code = HTTP_BAD_REQUEST;
        parser_err = make_gsl_err(gsl_FAIL);
        goto final;
    }

    /* reassign glosses */
    if (task->ctx->tr) {
        c->tr = task->ctx->tr;
        task->ctx->tr = NULL;
    }

    c->phase = KND_CLASS_IMPORTED;

    if (DEBUG_CLASS_IMPORT_LEVEL_3) {
        knd_log("++  {cls %.*s} import completed!", c->name_size, c->name);
    }

    switch (task->type) {
    case KND_TASK_RESTORE:
        // fall through
    case KND_TASK_COMMIT:
        err = knd_class_commit_state(c->entry, KND_CREATED, task);
        if (err) {
            return make_gsl_err_external(err);
        }
        break;
    default:
        break;
    }
    return make_gsl_err(gsl_OK);

 final:

    // TODO free resources
    
    return parser_err;
}
