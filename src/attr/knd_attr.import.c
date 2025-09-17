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
#include "knd_output.h"

#define DEBUG_ATTR_LEVEL_1 0
#define DEBUG_ATTR_LEVEL_2 0
#define DEBUG_ATTR_LEVEL_3 0
#define DEBUG_ATTR_LEVEL_4 0
#define DEBUG_ATTR_LEVEL_5 0
#define DEBUG_ATTR_LEVEL_TMP 1

struct LocalContext {
    struct kndClassBasePred *class_var;
    struct kndAttrStm  *list_owner;
    struct kndAttr     *attr;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

static gsl_err_t confirm_attr(void *obj,
                              const char *unused_var(name),
                              size_t unused_var(name_size))
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

static gsl_err_t set_class(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    if (!self->name_size) {
        knd_log("-- attr name not specified");
        return make_gsl_err(gsl_FAIL);
    }
    self->cls_name = name;
    self->cls_name_size = name_size;

    self->cls_name = name;
    self->cls_name_size = name_size;
    return make_gsl_err(gsl_OK);
}

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

static gsl_err_t run_set_quant(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. run set quant!\n");

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (!memcmp("set", name, name_size)) {
        self->quant_type = KND_ATTR_SET;
        self->is_a_set = true;
    }

    if (!memcmp("list", name, name_size)) {
        self->quant_type = KND_ATTR_LIST;
        self->is_a_set = true;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_quant_uniq(void *obj,
                                    const char *unused_var(name),
                                    size_t unused_var(name_size))
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. set is uniq");
    self->set_is_unique = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_quant_atomic(void *obj,
                                      const char *unused_var(name),
                                      size_t unused_var(name_size))
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. set is atomic");
    self->set_is_atomic = true;
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_parse_quant_type(void *obj, const char *rec, size_t *total_size)
{
    struct kndAttr *self = obj;
    if (!self->name_size) {
        knd_log("attr name not specified");
        return make_gsl_err(gsl_FAIL);
    }
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_quant,
          .obj = self
        },
        { .name = "uniq",
          .name_size = strlen("uniq"),
          .run = run_set_quant_uniq,
          .obj = self
        },
        { .name = "atom",
          .name_size = strlen("atom"),
          .run = run_set_quant_atomic,
          .obj = self
        },
        { .is_default = true,
          .run = confirm_attr,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t attr_is_required(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_required = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t attr_is_mult(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->quant_type = KND_ATTR_SET;
    self->is_a_set = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t attr_is_unique(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_unique = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_subtypes(void *obj, const char *name, size_t name_size,
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
    case KND_ATTR_CLS_REF:
        //err = knd_attr_cls_ref_import(attr->subtype, name, name_size, rec, total_size, ctx->task);
        //if (err) return *total_size = 0, make_gsl_err_external(err);    
        return make_gsl_err(gsl_OK);
    default:
        break;
    }

    KND_TASK_LOG("unknown {tag %.*s} in {attr %.*s}",
                 name_size, name, attr->name_size, attr->name);
    return make_gsl_err(gsl_FORMAT);
 }

gsl_err_t knd_attr_import(struct kndAttr *self, struct kndTask *task,
                          const char *rec, size_t *total_size)
{
    gsl_err_t err;

    if (DEBUG_ATTR_LEVEL_2) {
        knd_log(".. {cls %.*s} to import {attr-type %.*s}",
                self->owner->name_size, self->owner->name,
                strlen(knd_attr_names[self->type]), knd_attr_names[self->type]);
    }

    struct LocalContext ctx = {
        .attr = self,
        .task = task
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
          .obj = task
        },
        { .name = "format",
          .name_size = strlen("format"),
          .run = set_format,
          .obj = self
        },
        { .name = "cls",
          .name_size = strlen("cls"),
          .run = set_class,
          .obj = self
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_ref,
          .obj = self
        },
        { .name = "t",
          .name_size = strlen("t"),
          .parse = knd_parse_quant_type,
          .obj = self
        },
        { .name = "mult",
          .name_size = strlen("mult"),
          .run = attr_is_mult,
          .obj = self
        },
        { .name = "req",
          .name_size = strlen("req"),
          .run = attr_is_required,
          .obj = self
        },
        { .name = "uniq",
          .name_size = strlen("uniq"),
          .run = attr_is_unique,
          .obj = self
        },
        { .validate = parse_subtypes,
          .obj = &ctx
        }
    };

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) {
        switch (err.code) {
        case gsl_NO_MATCH:
            KND_TASK_LOG("unknown {tag %.*s} in {cls %.*s {attr %.*s}}",
                         err.val_size, err.val,
                         self->owner->name_size, self->owner->name,
                         self->name_size, self->name);
            break;
        default:
            break;
        }
        return err;
    }

    /* reassign glosses */
    if (task->ctx->tr) {
        self->tr = task->ctx->tr;
        task->ctx->tr = NULL;
    }

    switch (self->type) {
    case KND_ATTR_CLS_INNER:
        if (!self->cls_name_size) {
            KND_TASK_LOG("class not specified in {inner %.*s}", self->name_size, self->name);
            return make_gsl_err_external(knd_FORMAT);
        }
        break;
    default:
        break;
    }

    // TODO: reject attr names starting with an underscore _

    return make_gsl_err(gsl_OK);
}
