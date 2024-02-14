#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_repo.h"
#include "knd_user.h"
#include "knd_utils.h"

#include <assert.h>
#include <string.h>

#include "knd_task.h"
#include "knd_attr.h"
#include "knd_rel.h"
#include "knd_class.h"
#include "knd_proc.h"
#include "knd_repo.h"

#include <gsl-parser.h>

#define DEBUG_REL_IMPORT_LEVEL_1 0
#define DEBUG_REL_IMPORT_LEVEL_2 0
#define DEBUG_REL_IMPORT_LEVEL_TMP 1

struct LocalContext {
    struct kndClassVar *class_var;
    struct kndAttrVar  *list_parent;
    struct kndAttr     *attr;
    struct kndRel     *rel;
    struct kndTask     *task;
};

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = task->repo;
    struct kndAttr *attr = ctx->rel->attr;
    struct kndCharSeq *seq;
    int err;

    attr->name = name;
    attr->name_size = name_size;

    err = knd_charseq_fetch(repo, name, name_size, &seq, task);
    if (err) {
        KND_TASK_LOG("failed to encode a charseq %.*s", name_size, name);
        return make_gsl_err_external(err);
    }
    attr->seq = seq;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_proc_ref(void *obj, const char *name, size_t name_size)
{
    struct kndRel *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    self->ref_proc_name = name;
    self->ref_proc_name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_rel_inst_class(void *obj, const char *name, size_t name_size)
{
    struct kndRel *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    self->ref_classname = name;
    self->ref_classname_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_rel_subclass(void *obj, const char *name, size_t name_size)
{
    struct kndRel *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    self->ref_classname = name;
    self->ref_classname_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_subj_arg(void *obj, const char *name, size_t name_size)
{
    struct kndRel *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    self->subj_arg_name = name;
    self->subj_arg_name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_impl_arg(void *obj, const char *name, size_t name_size)
{
    struct kndRel *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    self->impl_arg_name = name;
    self->impl_arg_name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_ref(void *obj, const char *rec, size_t *total_size)
{
    struct kndRel *self = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_proc_ref,
          .obj = self
        },
        { .name = "subj",
          .name_size = strlen("subj"),
          .run = set_subj_arg,
          .obj = self
        },
        { .name = "impl-arg",
          .name_size = strlen("impl-arg"),
          .run = set_impl_arg,
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

gsl_err_t knd_rel_import(struct kndAttr *attr, struct kndTask *task,
                         const char *rec, size_t *total_size)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndRel *rel;
    gsl_err_t parser_err;
    int err;

    err = knd_rel_new(mempool, &rel);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->impl = rel;
    rel->attr = attr;

    struct LocalContext ctx = {
        .rel = rel,
        .task = task
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = knd_parse_gloss_array,
          .obj = task
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_ref,
          .obj = rel
        },
        { .name = "t",
          .name_size = strlen("t"),
          .parse = knd_parse_quant_type,
          .obj = attr
        },
        { .name = "idx",
          .name_size = strlen("idx"),
          .run = knd_attr_idx,
          .obj = attr
        },
        { .name = "impl",
          .name_size = strlen("impl"),
          .run = knd_attr_implied,
          .obj = attr
        },
        { .name = "req",
          .name_size = strlen("req"),
          .run = knd_attr_required,
          .obj = attr
        },
        { .name = "uniq",
          .name_size = strlen("uniq"),
          .run = knd_attr_unique,
          .obj = attr
        },
        { .name = "concise",
          .name_size = strlen("concise"),
          .parse = gsl_parse_size_t,
          .obj = &attr->concise_level
        }
    };

    if (DEBUG_REL_IMPORT_LEVEL_2)
        knd_log(".. rel parsing: \"%.*s\"..", 32, rec);

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    /* reassign glosses */
    if (task->ctx->tr) {
        attr->tr = task->ctx->tr;
        task->ctx->tr = NULL;
    }

    if (!attr->name_size) {
        KND_TASK_LOG("attr name not specified");
        return make_gsl_err(gsl_FORMAT);
    }
    if (!rel->ref_proc_name_size) {
        KND_TASK_LOG("proc name not specified in %.*s", attr->name_size, attr->name);
        return make_gsl_err(gsl_FORMAT);
    }
    if (!rel->subj_arg_name_size) {
        KND_TASK_LOG("subj arg name not specified in %.*s", attr->name_size, attr->name);
        return make_gsl_err(gsl_FORMAT);
    }
    if (!rel->impl_arg_name_size) {
        KND_TASK_LOG("implicit arg name not specified in %.*s", attr->name_size, attr->name);
        return make_gsl_err(gsl_FORMAT);
    }
    return make_gsl_err(gsl_OK);
}
