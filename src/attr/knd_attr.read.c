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
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_ignore.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"

#define DEBUG_ATTR_READ_LEVEL_1 0
#define DEBUG_ATTR_READ_LEVEL_2 0
#define DEBUG_ATTR_READ_LEVEL_3 0
#define DEBUG_ATTR_READ_LEVEL_4 0
#define DEBUG_ATTR_READ_LEVEL_5 0
#define DEBUG_ATTR_READ_LEVEL_TMP 1

struct LocalContext {
    const char *name;
    size_t name_size;
    struct kndClassBasePred *class_var;
    struct kndAttr     *attr;
    struct kndTask     *task;
};

static gsl_err_t set_attr_id(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttr *attr = ctx->attr;
    struct kndAttrRef *ref;
    int err;
    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(attr->id, id, id_size);
    attr->id_size = id_size;

    err = knd_set_get(task->idxs.attr_idx, id, id_size, (void**)&ref);
    if (err) {
        KND_TASK_LOG("failed to get {attr %.*s}", id_size, id);
        return make_gsl_err_external(err);
    }
    attr->name = ref->name;
    attr->name_size = ref->name_size;

    /*if (ref->attr) {
        knd_log("?? doublet {attr %.*s {id %.*s}} {owner %.*s}",
                ref->name_size, ref->name, ref->id_size, ref->id,
                attr->owner->name_size, attr->owner->name);
        return make_gsl_err(gsl_FAIL);
    } */
    // ref->attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_attr_ref_id(void *obj, const char *id, size_t id_size)
{
    struct kndAttrRef *ref = obj;
    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(ref->id, id, id_size);
    ref->id_size = id_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_owner_class_id(void *obj, const char *id, size_t id_size)
{
    struct kndAttrRef *ref = obj;
    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(ref->owner_id, id, id_size);
    ref->owner_id_size = id_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_template_cls(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttr *attr = ctx->attr;
    struct kndClassEntry *entry;
    struct kndTask *task = ctx->task;
    struct kndClassRefAttr *cls_ref_attr;
    struct kndClassInnerAttr *cls_inner_attr;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_FORMAT);

    err = knd_set_get(task->idxs.cls_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("no such {cls %.*s}", id_size, id);
        return make_gsl_err_external(err);
    }

    attr->cls_name = entry->name;
    attr->cls_name_size = entry->name_size;

    switch (attr->type) {
    case KND_ATTR_CLS_INNER:
        cls_inner_attr = attr->subtype;
        cls_inner_attr->template_cls = entry;
        break;
    case KND_ATTR_CLS_REF:
        cls_ref_attr = attr->subtype;
        cls_ref_attr->template_cls = entry;
        break;
    default:
        break;
    }
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
        .task = task
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_id,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "g",
          .name_size = strlen("g"),
          .parse = read_glosses,
          .obj = &ctx
        },
        { .name = "c",
          .name_size = strlen("c"),
          .run = set_template_cls,
          .obj = &ctx
        },
        { .name = "t",
          .name_size = strlen("t"),
          .parse = parse_quant_type,
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
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) {
        knd_log("-- failed to parse attr rec: %d", err.code);
        return err;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr_ref_array_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndDict *attr_name_idx = task->idxs.attr_name_idx;
    struct kndSet *attr_idx = task->idxs.attr_idx;
    struct kndAttrRef *ref, *refs;
    int err;

    err = knd_attr_ref_new(&ref, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_ref_id,
          .obj = ref
        },
        { .name = "c",
          .name_size = strlen("c"),
          .run = set_owner_class_id,
          .obj = ref
        }        
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    ref->name = ctx->name;
    ref->name_size = ctx->name_size;
    knd_calc_num_id(ref->id, ref->id_size, &ref->numid);

    if (DEBUG_ATTR_READ_LEVEL_3) {
        knd_log(".. register {attr %.*s}", ref->name_size, ref->name);
    }

    err = knd_dict_get(attr_name_idx, ref->name, ref->name_size, (void**)&refs, task);
    switch (err) {
    case knd_OK:
        if (refs->tail) {
            refs->tail->next = ref;
        } else {
            refs->next = ref;
        }
        refs->tail = ref;
        break;
    case knd_NO_MATCH:
        err = knd_dict_set(attr_name_idx, ref->name, ref->name_size, (void*)ref, task);
        if (err) {
            KND_TASK_LOG("failed to register {attr %.*s}", ref->name_size, ref->name);
            return make_gsl_err_external(err);
        }
        break;
    default:
        return make_gsl_err_external(err);
    }

    err = knd_set_add(attr_idx, ref->id, ref->id_size, (void*)ref, task);
    if (err) {
        KND_TASK_LOG("failed to register {attr-id %.*s} {err %d}",
                     ref->id_size, ref->id, err);
        return make_gsl_err_external(err);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr_ref_array(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    struct gslTaskSpec spec = {
        .is_list_item = true,
        .parse = parse_attr_ref_array_item,
        .obj = ctx
    };
    return gsl_parse_array(&spec, rec, total_size);
}

static gsl_err_t set_attr_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepoSnapshot *snapshot = task->snapshot;
    struct kndMemBlock *memblock;
    const char *b;
    int err;

    err = knd_repo_snapshot_fetch_memblock(snapshot, name_size, &memblock, task);
    if (err) {
        KND_TASK_LOG("failed to fetch a memblock to save attr name %.*s", name_size, name);
        return make_gsl_err_external(err);
    }

    err = knd_memblock_write(memblock, name, name_size, &b);
    if (err) {
        KND_TASK_LOG("failed to to save attr name %.*s", name_size, name);
        return make_gsl_err_external(err);
    }

    ctx->name = b;
    ctx->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr_name_array_item(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct LocalContext ctx = {
        .task = task
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_name,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "a",
          .name_size = strlen("a"),
          .parse = parse_attr_ref_array,
          .obj = &ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr_name_array(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    struct gslTaskSpec spec = {
        .is_list_item = true,
        .parse = parse_attr_name_array_item,
        .obj = ctx
    };
    return gsl_parse_array(&spec, rec, total_size);
}

int knd_attr_name_unmarshall(const char *unused_var(elem_id), size_t unused_var(elem_id_size),
                             const char *rec, size_t rec_size,
                             void *unused_var(ctx), void **unused_var(result), struct kndTask *task)
{
    size_t total_size = rec_size;

    if (DEBUG_ATTR_READ_LEVEL_2) {
        knd_log(">> attr name block: %.*s", rec_size, rec);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = knd_ignore_value,
          .obj = task
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "n",
          .name_size = strlen("n"),
          .parse = parse_attr_name_array,
          .obj = task
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, &total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err.code;

    return knd_OK;
}
