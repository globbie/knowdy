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
#include "knd_quant.h"
#include "knd_task.h"
#include "knd_query.h"
#include "knd_dict.h"
#include "knd_shared_dict.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_STM_SELECT_LEVEL_1 0
#define DEBUG_ATTR_STM_SELECT_LEVEL_2 0
#define DEBUG_ATTR_STM_SELECT_LEVEL_3 0
#define DEBUG_ATTR_STM_SELECT_LEVEL_4 0
#define DEBUG_ATTR_STM_SELECT_LEVEL_5 0
#define DEBUG_ATTR_STM_SELECT_LEVEL_TMP 1

struct LocalContext {
    struct kndQuery   *query;
    struct kndTask    *task;
    struct kndRepo    *repo;
    struct kndAttrStm *stm;

    struct kndClassInnerAttrStm *inner_stm;
    struct kndClassRefAttrStm *ref_stm;
    struct kndAttr    *attr;
};

static gsl_err_t check_inner_cls_query_range(void *obj, const char *unused_var(val),
                                             size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    struct kndAttrStm *stm = ctx->stm;

    stm->type = KND_ATTR_STM_QUERY_RANGE;

    if (DEBUG_ATTR_STM_SELECT_LEVEL_3) {
        knd_log(">> confirm default inner cls template - select all subclasses");
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t check_ref_cls_query_range(void *obj, const char *unused_var(val),
                                           size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    struct kndAttrStm *stm = ctx->stm;

    stm->type = KND_ATTR_STM_QUERY_RANGE;

    if (DEBUG_ATTR_STM_SELECT_LEVEL_3) {
        knd_log(">> confirm default ref cls template - select all subclasses");
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t check_inner_cls(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = task->repo;
    struct kndClassInnerAttrStm *inner = ctx->inner_stm;
    struct kndClassEntry *entry;
    int err;

    if (DEBUG_ATTR_STM_SELECT_LEVEL_TMP) {
        knd_log(">> set specific inner {cls %.*s}", name_size, name);
    }
    err = knd_get_class_entry(repo, name, name_size, true, &entry, task);
    if (err) {
        KND_TASK_LOG("{cls %.*s} not found", name_size, name);
        task->ctx->error = knd_NO_MATCH;
        return make_gsl_err(gsl_FAIL);
    }
    err = knd_class_acquire(entry, &inner->cls, task);
    if (err) {
        KND_TASK_LOG("failed to acquire {cls %.*s}", entry->name_size, entry->name);
        return make_gsl_err_external(err);
    }

    if (inner->cls == inner->template_cls) {
        // TODO raise a warning about tautology
        if (DEBUG_ATTR_STM_SELECT_LEVEL_TMP) {
            knd_log("NB: the same {cls %.*s} specified in inner cls spec",
                    inner->cls->name_size, inner->cls->name);
        }
        return make_gsl_err(gsl_OK);
    }

    err = knd_class_is_base(inner->template_cls, inner->cls);
    if (err) {
        KND_TASK_LOG("no inheritance from {cls %.*s} to {cls %.*s}",
                     inner->template_cls->name_size, inner->template_cls->name,
                     inner->cls->name_size, inner->cls->name);
        return make_gsl_err_external(err);
    }
   
    return make_gsl_err(gsl_OK);
}

static int check_ref_cls(const char *name, size_t name_size,
                         struct kndClassRefAttrStm *ref_stm,
                         struct kndTask *task)
{
    struct kndRepo *repo = task->repo;
    struct kndClassEntry *entry;
    struct kndClass *c;
    int err;

    err = knd_get_class_entry(repo, name, name_size, true, &entry, task);
    KND_TASK_ERR("{cls %.*s} not found", name_size, name);

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);

    if (c == ref_stm->template_cls) {
        // TODO raise a warning about tautology
        if (DEBUG_ATTR_STM_SELECT_LEVEL_TMP) {
            knd_log("NB: the same {cls %.*s} specified in ref cls spec",
                    c->name_size, c->name);
        }
        return knd_OK;
    }

    err = knd_class_is_base(ref_stm->template_cls, c);
    KND_TASK_ERR("no inheritance from {cls %.*s} to {cls %.*s}",
                 ref_stm->template_cls->name_size, ref_stm->template_cls->name,
                 c->name_size, c->name);

    ref_stm->cls_entry = entry;
    ref_stm->cls = c;
    return knd_OK;
}

static gsl_err_t check_ref_cls_cb(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    int err;

    if (DEBUG_ATTR_STM_SELECT_LEVEL_3) {
        knd_log(">> check and set specific ref {cls %.*s}", name_size, name);
    }

    err = check_ref_cls(name, name_size, ctx->ref_stm, task);
    if (err) {
        task->ctx->error = err;
        return make_gsl_err(gsl_FAIL);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_inner_cls_attr_stm(void *obj, const char *name, size_t name_size,
                                           const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrStm *parent = ctx->stm;
    struct kndClassInnerAttrStm *inner_stm = ctx->inner_stm;
    struct kndAttrStm *stm;
    struct kndTask    *task = ctx->task;
    struct kndClass *c = inner_stm->cls ? inner_stm->cls : inner_stm->template_cls;
    struct kndAttr *attr;
    int err;

    assert (c != NULL);
    
    err = knd_attr_find(c, name, name_size, &attr, task);
    if (err) {
        KND_TASK_LOG("{attr %.*s} is not applicable to {cls %.*s}",
                     name_size, name, c->name_size, c->name);
        return make_gsl_err(gsl_FAIL);
    }

    if (DEBUG_ATTR_STM_SELECT_LEVEL_3) {
        knd_log("{cls %.*s {attr %.*s}} confirmed by owner {cls %.*s}",
                c->name_size, c->name, name_size, name,
                attr->owner->name_size, attr->owner->name);

        if (attr->facet) {
            knd_facet_str(attr->facet, knd_attr_stm_present_subj, 0);
        }
    }

    err = knd_attr_stm_new(&stm, c, task->mempool);
    if (err) return make_gsl_err_external(err);   
    stm->attr = attr;

    err = knd_attr_parse_query_stm(stm, rec, total_size, task);
    if (err) return make_gsl_err_external(err);

    stm->next = parent->children;
    parent->children = stm;
    parent->num_children++;
    return make_gsl_err(gsl_OK);
}

static int inner_cls_parse(struct kndAttrStm *stm, struct kndClassInnerAttrStm *inner,
                           const char *rec, size_t *total_size, struct kndTask *task)
{
    gsl_err_t parser_err;

    struct LocalContext ctx = {
        .task = task,
        .stm = stm,
        .inner_stm = inner
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_inner_cls,
          .obj = &ctx
        },
        { .validate = select_inner_cls_attr_stm,
          .obj = &ctx
        },
        { .is_default = true,
          .run = check_inner_cls_query_range,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err.code;
    
    return knd_OK;
}

static int ref_cls_parse(struct kndAttrStm *stm, struct kndClassRefAttrStm *ref,
                         const char *rec, size_t *total_size, struct kndTask *task)
{
    gsl_err_t parser_err;

    struct LocalContext ctx = {
        .task = task,
        .stm = stm,
        .ref_stm = ref
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_ref_cls_cb,
          .obj = &ctx
        },
        { .is_default = true,
          .run = check_ref_cls_query_range,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err.code;
    
    return knd_OK;
}

int knd_attr_parse_query_stm(struct kndAttrStm *stm,
                             const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndAttr *attr = stm->attr;
    struct kndQuantAttrStm *quant_attr_stm;
    struct kndClassRefAttrStm *cref;
    struct kndClassInnerAttrStm *inner;
    struct kndClassInnerAttr *inner_attr;
    struct kndClassRefAttr *ref_attr;
    struct kndClassEntry *entry;
    int err;

    if (DEBUG_ATTR_STM_SELECT_LEVEL_TMP) {
        knd_log(".. parse attr stm query {cls %.*s {attr %.*s}}",
                stm->subj->name_size, stm->subj->name,
                attr->owner->name_size, attr->owner->name,
                attr->name_size, attr->name);
    }

    switch (attr->type) {
    case KND_ATTR_CLS_INNER:
        err = knd_cls_inner_attr_stm_new(&inner, task->mempool);
        KND_TASK_ERR("failed to alloc a cls inner attr stm");
        stm->subtype = inner;

        inner_attr = attr->subtype;
        entry = inner_attr->template_cls;

        err = knd_class_acquire(entry, &inner->template_cls, task);
        KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);

        err = inner_cls_parse(stm, inner, rec, total_size, task);
        KND_TASK_ERR("failed to parse inner cls stm");
        break;
    case KND_ATTR_CLS_REF:
        err = knd_cls_ref_attr_stm_new(&cref, task->mempool);
        KND_TASK_ERR("failed to alloc a cls ref attr stm");
        stm->subtype = cref;

        ref_attr = attr->subtype;
        entry = ref_attr->template_cls;

        err = knd_class_acquire(entry, &cref->template_cls, task);
        KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);

        err = ref_cls_parse(stm, cref, rec, total_size, task);
        KND_TASK_ERR("failed to parse cls ref stm");
        break;
    case KND_ATTR_UINT:
        err = knd_quant_attr_stm_new(&quant_attr_stm, task->mempool);
        KND_TASK_ERR("failed to alloc a quant attr stm");
        stm->subtype = quant_attr_stm;

        err = knd_quant_uint_parse_stm(quant_attr_stm, rec, total_size, task);
        KND_TASK_ERR("failed to parse uint stm");
        break;
    case KND_ATTR_UREAL:
        // TODO
        break;
    default:
        knd_log("-- no clause filtering in attr %.*s",
                attr->name_size, attr->name);
        return knd_FAIL;
    }

    return knd_OK;
}

static int filter_subj(void *elem, void *ctx_obj)
{
    struct kndAttrStm *stm = elem;
    struct LocalContext *ctx = ctx_obj;
    struct kndClass *c = stm->subj;
    struct kndClass *bc = ctx->stm->subj;
    int err;

    if (DEBUG_ATTR_STM_SELECT_LEVEL_2) {
        knd_log("!! filter subj {cls %.*s} with base {cls %.*s}",
                c->name_size, c->name, bc->name_size, bc->name);
    }

    if (c != bc) {
        return knd_class_is_base(bc, c);
    }
    return knd_OK;
}

static int cls_ref_query_plan(struct kndAttrStm *stm, struct kndFacet *facet,
                              struct kndTask *task)
{
    struct kndClassRefAttrStm *cref = stm->subtype;
    struct kndClassRefAttr *cls_ref_attr = stm->attr->subtype;
    struct kndClassEntry *entry = cref->cls_entry ? cref->cls_entry : cls_ref_attr->template_cls;
    assert (entry != NULL);

    size_t depth = 0;
    int err;

    struct LocalContext ctx = {
        .task = task,
        .stm = stm,
        .ref_stm = cref
    };

    err = knd_facet_map(facet, entry, NULL,
                        filter_subj, &ctx,
                        knd_attr_stm_present_subj, &ctx, task);
    KND_TASK_ERR("failed to map facet fn");

    return knd_OK;
}

int knd_attr_stm_plan(struct kndAttrStm *stm, struct kndTask *task)
{
    struct kndAttr *attr = stm->attr;
    struct kndFacet *facet = attr->facet;
    struct kndQuantAttrStm *quant_attr_stm;
    struct kndClassRefAttr *cls_ref_attr;
    struct kndClassRefAttrStm *cref;
    struct kndClassEntry *entry;
    int err;

    if (DEBUG_ATTR_STM_SELECT_LEVEL_TMP) {
        knd_log(".. query planning of {cls %.*s {attr %.*s {type %s}}}",
                stm->subj->name_size, stm->subj->name,
                attr->name_size, attr->name, knd_attr_names[attr->type]);
    }

    if (!facet) {
        knd_log("no facets exist for {attr %.*s}", attr->name_size, attr->name);
        return knd_OK;
    }

    switch (attr->type) {
    case KND_ATTR_UINT:
        quant_attr_stm = stm->subtype;

        err = knd_quant_uint_query_plan(quant_attr_stm, facet, task);
        KND_TASK_ERR("failed to plan a quant uint query");

        if (quant_attr_stm->match) {
            stm->match = quant_attr_stm->match;
        }
        break;
    case KND_ATTR_CLS_REF:
        err = cls_ref_query_plan(stm, facet, task);
        KND_TASK_ERR("failed to plan a cls ref query");
        break;
    default:
        break;
    }
    return knd_OK;
}

int knd_facet_cls_key_get(void *elem, void **result, struct kndTask *task)
{
    struct kndAttrStm *stm = elem;
    struct kndAttr *attr = stm->is_list_item ? stm->parent->attr : stm->attr;
    struct kndClassInnerAttr *cls_inner_attr;
    struct kndClassInnerAttrStm *inner_stm;
    struct kndClassRefAttr *cls_ref_attr;
    struct kndClassRefAttrStm *ref_stm;
    struct kndClassEntry *entry;
    int err;

    switch (attr->type) {
    case KND_ATTR_CLS_INNER:
        cls_inner_attr = attr->subtype;
        inner_stm = stm->subtype;
        entry = inner_stm->cls_entry ? inner_stm->cls_entry : cls_inner_attr->template_cls;

        *result = entry;
        return knd_OK;
    case KND_ATTR_CLS_REF:
        cls_ref_attr = attr->subtype;
        ref_stm = stm->subtype;
        entry = ref_stm->cls_entry ? ref_stm->cls_entry : cls_ref_attr->template_cls;

        *result = entry;
        return knd_OK;
    default:
        break;
    }
    return knd_NO_MATCH;
}
