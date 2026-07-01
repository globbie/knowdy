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
#include "knd_memblock.h"
#include "knd_text.h"
#include "knd_quant.h"
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
    struct kndTask     *task;
    struct kndTask     *repo;
    struct kndAttr     *attr;
    struct kndAttrRef  *attr_refs;
    const char *id;
    size_t id_size;
};

static gsl_err_t set_attr_name_id(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttr *attr = ctx->attr;
    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);
    memcpy(attr->name_id, id, id_size);
    attr->name_id_size = id_size;
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

static gsl_err_t confirm_attr(void *unused_var(obj), const char *unused_var(name), size_t unused_var(name_size))
{
    return make_gsl_err(gsl_OK);
}

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

static gsl_err_t parse_gloss(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttr *attr = ctx->attr;
    struct kndText *t;
    int err;

    err = knd_text_new(&t, task->cache.mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc a text");
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = knd_gloss_parse(t, rec, total_size, task);
    if (err) {
        KND_TASK_LOG("failed to parse gloss");
        return *total_size = 0, make_gsl_err_external(err);
    }

    t->next = attr->glosses;
    attr->glosses = t;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_glosses(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec spec = {
        .is_list_item = true,
        .parse = parse_gloss,
        .obj = obj
    };
    return gsl_parse_array(&spec, rec, total_size);
}

static int select_attr_type(struct kndAttr *attr, size_t type_num, struct kndTask *task)
{
    struct kndMemPool *mempool = task->cache.mempool;
    struct kndQuantAttr *quant_attr;
    struct kndClassRefAttr *cls_ref_attr;
    struct kndClassInnerAttr *cls_inner_attr;
    int err;

    if (type_num >= KND_ATTR_SENTINEL) return knd_FORMAT;

    attr->type = (knd_attr_type)type_num;

    switch (attr->type) {
    case KND_ATTR_NONE:
        knd_log("{attr-type %zu} is not supported", type_num);
        return knd_CONFLICT;
    case KND_ATTR_UINT:
        err = knd_quant_attr_new(&quant_attr, KND_QUANT_UINT, attr->id, attr->id_size, mempool);
        KND_TASK_ERR("failed to alloc a quant attr");
        attr->subtype = quant_attr;
        break;
    case KND_ATTR_UREAL:
        err = knd_quant_attr_new(&quant_attr, KND_QUANT_UREAL, attr->id, attr->id_size, mempool);
        KND_TASK_ERR("failed to alloc a quant attr");
        attr->subtype = quant_attr;
        break;
    case KND_ATTR_CLS_INNER:
        err = knd_cls_inner_attr_new(&cls_inner_attr, attr->id, attr->id_size, mempool);
        KND_TASK_ERR("failed to alloc a cls inner attr");
        attr->subtype = cls_inner_attr;
        break;
    case KND_ATTR_CLS_REF:
        err = knd_cls_ref_attr_new(&cls_ref_attr, attr->id, attr->id_size, mempool);
        KND_TASK_ERR("failed to alloc a ref attr");
        attr->subtype = cls_ref_attr;
        break;
    default:
        break;
    }
    return knd_OK;
}

static int attr_read(struct kndAttr *attr, const char *rec, size_t *total_size, struct kndTask *task)
{
    size_t attr_type_num = 0;
    char id[KND_ID_SIZE];
    size_t id_size = 0;
    struct kndClassRefAttr *cls_ref_attr;
    struct kndClassInnerAttr *cls_inner_attr;
    int err;
    gsl_err_t parser_err;

    struct LocalContext ctx = {
        .attr = attr,
        .task = task
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_name_id,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "g",
          .name_size = strlen("g"),
          .parse = parse_glosses,
          .obj = &ctx
        },
        { .name = "t",
          .name_size = strlen("t"),
          .parse = gsl_parse_size_t,
          .obj = &attr_type_num
        },
        { .name = "c",
          .name_size = strlen("c"),
          .buf = id,
          .buf_size = &id_size,
          .max_buf_size = KND_ID_SIZE
        },
        { .name = "m",
          .name_size = strlen("m"),
          .parse = parse_mult_type,
          .obj = attr
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    err = select_attr_type(attr, attr_type_num, task);
    KND_TASK_ERR("failed to select attr type {num %zu}", attr_type_num);

    if (id_size) {
        switch (attr->type) {
        case KND_ATTR_CLS_REF:
            cls_ref_attr = attr->subtype;
            memcpy(cls_ref_attr->cls_id, id, id_size);
            cls_ref_attr->cls_id_size = id_size;        
            break;
        case KND_ATTR_CLS_INNER:
            cls_inner_attr = attr->subtype;
            memcpy(cls_inner_attr->cls_id, id, id_size);
            cls_inner_attr->cls_id_size = id_size;
            break;
        default:
            err = knd_FORMAT;
            KND_TASK_ERR("cls template not supported in {attr %d}", attr->type);
        }
    }

    return knd_OK;
}

static gsl_err_t parse_attr_ref_array_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrRef *ref;
    int err;

    err = knd_attr_ref_new(&ref, task->cache.mempool);
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

    //knd_calc_num_id(ref->id, ref->id_size, &ref->numid);

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

int knd_attr_name_fetch(const char *rec, size_t unused_var(rec_size), const char *key, size_t key_size,
                        void *unused_var(ctx), size_t *result_size, void **result, struct kndTask *task)
{
    char namebuf[KND_NAME_SIZE];
    size_t namebuf_size = 0;

    if (DEBUG_ATTR_READ_LEVEL_2) {
        knd_log(">> attr name block: %s", rec);
    }

    struct LocalContext local_ctx = {
        .task = task
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = namebuf,
          .buf_size = &namebuf_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "a",
          .name_size = strlen("a"),
          .parse = parse_attr_ref_array,
          .obj = &local_ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, result_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err.code;

    if (key_size != namebuf_size) return knd_NO_MATCH;
    if (memcmp(key, namebuf, namebuf_size)) return knd_NO_MATCH;

    if (DEBUG_ATTR_READ_LEVEL_2) {
        knd_log("++ {attr %.*s} matched", namebuf_size, namebuf);
    }

    if (!local_ctx.attr_refs) return knd_FAIL;

    *result = local_ctx.attr_refs;

    return knd_OK;
}

int knd_attr_unmarshall(const char *elem_id, size_t elem_id_size,
                         const char *rec, size_t unused_var(rec_size),
                        void *ctx_obj, size_t *parsed_size, void **result, struct kndTask *task)
{
    struct LocalContext *ctx = ctx_obj;
    struct kndAttr *attr;
    int err;

    if (DEBUG_ATTR_READ_LEVEL_2) {
        knd_log(".. unmarshall {attr %.*s {rec %s}}", ctx->id_size, ctx->id, rec);
    }

    err = knd_attr_new(&attr, task->cache.mempool);
    KND_TASK_ERR("failed to alloc an attr to unmarshall");
    memcpy(attr->id, ctx->id, ctx->id_size);
    attr->id_size = ctx->id_size;

    err = attr_read(attr, rec, parsed_size, task);
    KND_TASK_ERR("failed to read GSP of {attr %.*s}", elem_id_size, elem_id);

    if (DEBUG_ATTR_READ_LEVEL_3) {
        const char *attr_type_name = knd_attr_names[attr->type];
        size_t attr_type_name_size = strlen(attr_type_name);
        knd_log("++ {attr %.*s {type %.*s}} unmarshalled OK!",
                attr->id_size, attr->id, attr_type_name_size, attr_type_name);
    }

    *result = attr;
    return knd_OK;
}
