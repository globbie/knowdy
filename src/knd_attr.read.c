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
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#define DEBUG_ATTR_READ_LEVEL_1 0
#define DEBUG_ATTR_READ_LEVEL_2 0
#define DEBUG_ATTR_READ_LEVEL_3 0
#define DEBUG_ATTR_READ_LEVEL_4 0
#define DEBUG_ATTR_READ_LEVEL_5 0
#define DEBUG_ATTR_READ_LEVEL_TMP 1

struct LocalContext {
    struct kndClassVar *class_var;
    struct kndAttr     *attr;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttr *self = ctx->attr;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = task->repo;
    struct kndCharSeq *seq;
    int err;

    self->name = name;
    self->name_size = name_size;

    if (name_size <= KND_ID_SIZE) {
        err = knd_shared_set_get(repo->str_idx, name, name_size, (void**)&seq);
        if (err) {
            KND_TASK_LOG("failed to decode attr name code \"%.*s\"", name_size, name);
            return make_gsl_err_external(err);
        }
        self->name = seq->val;
        self->name_size = seq->val_size;
        self->seq = seq;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_ref_class(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttr *self = ctx->attr;
    struct kndRepo *repo = ctx->repo;
    struct kndClassEntry *entry;
    struct kndTask *task = ctx->task;
    int err;
    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_FORMAT);

    err = knd_shared_set_get(repo->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("failed to link class entry \"%.*s\"", id_size, id);
        return make_gsl_err_external(err);
    }
    self->ref_classname = entry->name;
    self->ref_classname_size = entry->name_size;
    self->ref_class_entry = entry;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_idx(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_indexed = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_implied(void *obj,
                                 const char *unused_var(name),
                                 size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_implied = true;
    return make_gsl_err(gsl_OK);
}
static gsl_err_t confirm_required(void *obj,
                                  const char *unused_var(name),
                                  size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_required = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_unique(void *obj,
                                const char *unused_var(name),
                                size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_unique = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_quant(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    if (!memcmp("set", name, name_size)) {
        self->quant_type = KND_ATTR_SET;
        self->is_a_set = true;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_quant_uniq(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = (struct kndAttr*)obj;
    self->set_is_unique = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_quant_atomic(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->set_is_atomic = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *attr = obj;
    if (DEBUG_ATTR_READ_LEVEL_2)
        knd_log("++ confirm attr: %.*s",
                attr->name_size, attr->name);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_id(void *obj, const char *rec, size_t *total_size)
{
    struct kndAttr *self = obj;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .buf = self->id,
            .buf_size = &self->id_size,
            .max_buf_size = KND_ID_SIZE
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_quant_type(void *obj, const char *rec, size_t *total_size)
{
    struct kndAttr *self = obj;
    if (!self->name_size) {
        knd_log("-- attr name not specified");
        return make_gsl_err(gsl_FAIL);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_quant,
          .obj = self
        },
        { .name = "uniq",
          .name_size = strlen("uniq"),
          .run = set_quant_uniq,
          .obj = self
        },
        { .name = "atom",
          .name_size = strlen("atom"),
          .run = set_quant_atomic,
          .obj = self
        },
        { .is_default = true,
          .run = confirm_attr,
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t read_glosses(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttr *self = ctx->attr;
    gsl_err_t parser_err;

    parser_err = knd_read_gloss_array((void*)task, rec, total_size);
    if (parser_err.code) return *total_size = 0, parser_err;

    if (task->ctx->tr) {
        self->tr = task->ctx->tr;
        task->ctx->tr = NULL;
    }
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_attr_read(struct kndAttr *self, struct kndTask *task, const char *rec, size_t *total_size)
{
    struct LocalContext ctx = {
        .attr = self,
        .repo = task->repo,
        .task = task
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = &ctx
        },
        { .name = "id",
          .name_size = strlen("id"),
          .parse = parse_id,
          .obj = self
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = read_glosses,
          .obj = &ctx
        },
        { .name = "c",
          .name_size = strlen("c"),
          .run = set_ref_class,
          .obj = &ctx
        },
        { .name = "t",
          .name_size = strlen("t"),
          .parse = parse_quant_type,
          .obj = self
        },
        { .name = "idx",
          .name_size = strlen("idx"),
          .run = confirm_idx,
          .obj = self
        },
        { .name = "impl",
          .name_size = strlen("impl"),
          .run = confirm_implied,
          .obj = self
        },
        { .name = "req",
          .name_size = strlen("req"),
          .run = confirm_required,
          .obj = self
        },
        { .name = "uniq",
          .name_size = strlen("uniq"),
          .run = confirm_unique,
          .obj = self
        },
        { .name = "concise",
          .name_size = strlen("concise"),
          .parse = gsl_parse_size_t,
          .obj = &self->concise_level
        }
    };
    gsl_err_t err;

    if (DEBUG_ATTR_READ_LEVEL_2)
        knd_log(".. attr parsing: \"%.*s\"..", 32, rec);

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (self->type == KND_ATTR_INNER) {
        if (!self->ref_classname_size) {
            knd_log("-- ref class not specified in %.*s",
                    self->name_size, self->name);
            return make_gsl_err_external(knd_FAIL);
        }
    }

    // TODO: reject attr names starting with an underscore _

    return make_gsl_err(gsl_OK);
}
