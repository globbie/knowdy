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
    struct kndClassVar *class_var;
    struct kndAttrVar  *list_parent;
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
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = task->repo;
    struct kndAttr *self = ctx->attr;
    struct kndCharSeq *seq;
    int err;

    self->name = name;
    self->name_size = name_size;

    err = knd_charseq_fetch(repo, name, name_size, &seq, task);
    if (err) {
        KND_TASK_LOG("failed to encode a charseq %.*s", name_size, name);
        return make_gsl_err_external(err);
    }
    self->seq = seq;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_ref_class(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    if (!self->name_size) {
        knd_log("-- attr name not specified");
        return make_gsl_err(gsl_FAIL);
    }
    self->ref_classname = name;
    self->ref_classname_size = name_size;
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

gsl_err_t knd_attr_idx(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_indexed = true;
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_attr_implied(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_implied = true;
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_attr_required(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_required = true;
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_attr_unique(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_unique = true;
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_attr_import(struct kndAttr *self, struct kndTask *task,
                          const char *rec, size_t *total_size)
{
    if (DEBUG_ATTR_LEVEL_1) {
        knd_log(".. {class %.*s} to import {attr-type %.*s}",
                self->parent->name_size, self->parent->name,
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
        { .name = "cls",
          .name_size = strlen("cls"),
          .run = set_ref_class,
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
        { .name = "idx",
          .name_size = strlen("idx"),
          .run = knd_attr_idx,
          .obj = self
        },
        { .name = "impl",
          .name_size = strlen("impl"),
          .run = knd_attr_implied,
          .obj = self
        },
        { .name = "req",
          .name_size = strlen("req"),
          .run = knd_attr_required,
          .obj = self
        },
        { .name = "uniq",
          .name_size = strlen("uniq"),
          .run = knd_attr_unique,
          .obj = self
        },
        { .name = "concise",
          .name_size = strlen("concise"),
          .parse = gsl_parse_size_t,
          .obj = &self->concise_level
        }
    };
    gsl_err_t err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. attr parsing: \"%.*s\"..", 32, rec);

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) {
        switch (err.code) {
        case gsl_NO_MATCH:
            KND_TASK_LOG("unknown \"%.*s\" tag in {class %.*s {attr %.*s}}",
                         err.val_size, err.val,
                         self->parent->name_size, self->parent->name,
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
    case KND_ATTR_INNER:
        if (!self->ref_classname_size) {
            KND_TASK_LOG("ref class not specified in %.*s", self->name_size, self->name);
            return make_gsl_err_external(knd_FORMAT);
        }
        break;
    default:
        break;
    }

    // TODO: reject attr names starting with an underscore _

    return make_gsl_err(gsl_OK);
}
