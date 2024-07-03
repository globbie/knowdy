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

static gsl_err_t set_attr_id(void *obj, const char *id, size_t id_size)
{
    struct kndAttr *attr = obj;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(attr->id, id, id_size);
    attr->id_size = id_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_ref_class(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttr *attr = ctx->attr;
    struct kndClassEntry *entry;
    struct kndTask *task = ctx->task;
    int err;
    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_FORMAT);

    err = knd_shared_set_get(task->idxs->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("failed to link class entry \"%.*s\"", id_size, id);
        return make_gsl_err_external(err);
    }

    attr->ref_classname = entry->name;
    attr->ref_classname_size = entry->name_size;
    attr->ref_class_entry = entry;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_idx(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *attr = obj;
    attr->is_indexed = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_implied(void *obj,
                                 const char *unused_var(name),
                                 size_t unused_var(name_size))
{
    struct kndAttr *attr = obj;
    attr->is_implied = true;
    return make_gsl_err(gsl_OK);
}
static gsl_err_t confirm_required(void *obj,
                                  const char *unused_var(name),
                                  size_t unused_var(name_size))
{
    struct kndAttr *attr = obj;
    attr->is_required = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_unique(void *obj,
                                const char *unused_var(name),
                                size_t unused_var(name_size))
{
    struct kndAttr *attr = obj;
    attr->is_unique = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_quant(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *attr = (struct kndAttr*)obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    if (!memcmp("set", name, name_size)) {
        attr->quant_type = KND_ATTR_SET;
        attr->is_a_set = true;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_quant_uniq(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *attr = (struct kndAttr*)obj;
    attr->set_is_unique = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_quant_atomic(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *attr = obj;
    attr->set_is_atomic = true;
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

static gsl_err_t parse_quant_type(void *obj, const char *rec, size_t *total_size)
{
    struct kndAttr *attr = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_quant,
          .obj = attr
        },
        { .name = "uniq",
          .name_size = strlen("uniq"),
          .run = set_quant_uniq,
          .obj = attr
        },
        { .name = "atom",
          .name_size = strlen("atom"),
          .run = set_quant_atomic,
          .obj = attr
        },
        { .is_default = true,
          .run = confirm_attr,
          .obj = attr
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t read_glosses(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttr *attr = ctx->attr;
    gsl_err_t parser_err;

    parser_err = knd_read_gloss_array((void*)task, rec, total_size);
    if (parser_err.code) return *total_size = 0, parser_err;

    if (task->ctx->tr) {
        attr->tr = task->ctx->tr;
        task->ctx->tr = NULL;
    }
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_attr_read(struct kndAttr *attr, struct kndTask *task,
                        const char *rec, size_t *total_size)
{
    struct LocalContext ctx = {
        .attr = attr,
        .repo = task->repo,
        .task = task
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_id,
          .obj = attr
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
        { .name = "rc",
          .name_size = strlen("rc"),
          .run = set_ref_class,
          .obj = &ctx
        },
        { .name = "t",
          .name_size = strlen("t"),
          .parse = parse_quant_type,
          .obj = attr
        },
        { .name = "idx",
          .name_size = strlen("idx"),
          .run = confirm_idx,
          .obj = attr
        },
        { .name = "impl",
          .name_size = strlen("impl"),
          .run = confirm_implied,
          .obj = attr
        },
        { .name = "req",
          .name_size = strlen("req"),
          .run = confirm_required,
          .obj = attr
        },
        { .name = "uniq",
          .name_size = strlen("uniq"),
          .run = confirm_unique,
          .obj = attr
        },
        { .name = "concise",
          .name_size = strlen("concise"),
          .parse = gsl_parse_size_t,
          .obj = &attr->concise_level
        }
    };
    gsl_err_t err;

    if (DEBUG_ATTR_READ_LEVEL_2)
        knd_log(".. attr parsing: \"%.*s\"..", 32, rec);

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (attr->type == KND_ATTR_INNER) {
        if (!attr->ref_classname_size) {
            knd_log("-- ref class not specified in %.*s",
                    attr->name_size, attr->name);
            return make_gsl_err_external(knd_FAIL);
        }
    }

    // TODO: reject attr names starting with an underscore _

    return make_gsl_err(gsl_OK);
}
