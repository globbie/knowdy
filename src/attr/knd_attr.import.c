#include "knd_attr.h"

#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_shared_set.h"
#include "knd_set.h"
#include "knd_repo.h"
#include "knd_user.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#include <assert.h>
#include <string.h>

#include "knd_task.h"
#include "knd_class.h"
#include "knd_quant.h"
#include "knd_proc.h"
#include "knd_text.h"
#include "knd_ignore.h"
#include "knd_output.h"

#define DEBUG_ATTR_LEVEL_1 0
#define DEBUG_ATTR_LEVEL_2 0
#define DEBUG_ATTR_LEVEL_3 0
#define DEBUG_ATTR_LEVEL_4 0
#define DEBUG_ATTR_LEVEL_5 0
#define DEBUG_ATTR_LEVEL_TMP 1

struct LocalContext {
    struct kndRepoSnapshot *snapshot;
    struct kndTask     *task;
    struct kndAttr     *attr;
};

static gsl_err_t confirm_attr(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *attr = obj;

    if (DEBUG_ATTR_LEVEL_2) {
        knd_log("++ confirm attr: %.*s",
                attr->name_size, attr->name);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttr *self = ctx->attr;
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_format(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    if (!self->name_size) {
        knd_log("-- attr name not specified");
        return make_gsl_err(gsl_FAIL);
    }
    self->format_cls_name = name;
    self->format_cls_name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_template_cls(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *attr = obj;
    struct kndClassRefAttr *cls_ref_attr;
    struct kndClassInnerAttr *cls_inner_attr;

    switch (attr->type) {
    case KND_ATTR_CLS_REF:
        cls_ref_attr = attr->subtype;
        cls_ref_attr->cls_name = name;
        cls_ref_attr->cls_name_size = name_size;        
        break;
    case KND_ATTR_CLS_INNER:
        cls_inner_attr = attr->subtype;
        cls_inner_attr->cls_name = name;
        cls_inner_attr->cls_name_size = name_size;
        break;
    default:
       return make_gsl_err(gsl_FORMAT);
    }
    return make_gsl_err(gsl_OK);
}

#if 0
static gsl_err_t set_proc_ref(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    self->ref_proc_name = name;
    self->ref_proc_name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_ref(void *obj, const char *rec, size_t *total_size)
{
    struct kndAttr *self = obj;

    if (!self->name_size) {
        knd_log("-- attr name not specified");
        return make_gsl_err(gsl_FAIL);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_proc_ref,
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
#endif

static gsl_err_t parse_mult_type(void *obj, const char *rec, size_t *total_size)
{
    struct kndAttr *attr = obj;

    attr->mult_t = KND_ATTR_MULTIPLE;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = knd_ignore_value,
          .obj = attr
        },
        { .is_default = true,
          .run = confirm_attr,
          .obj = attr
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_subtype_field(void *obj, const char *name, size_t name_size,
                                     const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttr *attr = ctx->attr;
    struct kndTask *task = ctx->task;
    int err;

    if (DEBUG_ATTR_LEVEL_2) {
        knd_log(".. {attr-type %.*s {spec %.*s}}",
                strlen(knd_attr_names[attr->type]), knd_attr_names[attr->type],
                name_size, name);
    }

    switch (attr->type) {
    case KND_ATTR_UINT:
        // fall through
    case KND_ATTR_UREAL:
        err = knd_quant_attr_setting_import(attr->subtype, name, name_size, rec, total_size, ctx->task);
        if (err) return *total_size = 0, make_gsl_err_external(err);    
        return make_gsl_err(gsl_OK);
    default:
        break;
    }

    KND_TASK_LOG("unknown {tag %.*s} in {attr %.*s}",
                 name_size, name, attr->name_size, attr->name);
    return make_gsl_err(gsl_FORMAT);
}

int knd_attr_import(struct kndAttr *attr, const char *rec, size_t *total_size,
                    struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndCharSeq *seq;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_ATTR_LEVEL_2) {
        knd_log(".. {cls %.*s} to import {attr-type %.*s}",
                attr->owner->name_size, attr->owner->name,
                strlen(knd_attr_names[attr->type]), knd_attr_names[attr->type]);
    }

    struct LocalContext ctx = {
        .snapshot = snapshot,
        .task = task,
        .attr = attr
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "gloss",
          .name_size = strlen("gloss"),
          .parse = knd_parse_gloss_array,
          .obj = &ctx
        },
        { .name = "format",
          .name_size = strlen("format"),
          .run = set_format,
          .obj = attr
        },
        { .name = "cls",
          .name_size = strlen("cls"),
          .run = set_template_cls,
          .obj = attr
        },
        { .name = "mult",
          .name_size = strlen("mult"),
          .parse = parse_mult_type,
          .obj = attr
        },
        { .validate = parse_subtype_field,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        switch (parser_err.code) {
        case gsl_NO_MATCH:
            KND_TASK_LOG("unknown {tag %.*s} in {cls %.*s {attr %.*s}}",
                         parser_err.val_size, parser_err.val,
                         attr->owner->name_size, attr->owner->name,
                         attr->name_size, attr->name);
            break;
        default:
            break;
        }
        return gsl_err_to_knd_err_codes(parser_err);
    }

    /* reassign glosses */
    if (task->ctx->tr) {
        attr->glosses = task->ctx->tr;
        task->ctx->tr = NULL;
    }

    err = knd_charseq_register(snapshot, attr->name, attr->name_size, &seq, task);
    KND_TASK_ERR("failed to encode attr name {seq %.*s}", attr->name_size, attr->name);
    attr->seq = seq;
    attr->name = seq->val;
    attr->name_size = seq->val_size;

    return knd_OK;
}
