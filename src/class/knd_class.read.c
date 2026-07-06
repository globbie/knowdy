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
#include "knd_memblock.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_quant.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_ignore.h"
#include "knd_shared_dict.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_CLASS_READ_LEVEL_1 0
#define DEBUG_CLASS_READ_LEVEL_2 0
#define DEBUG_CLASS_READ_LEVEL_3 0
#define DEBUG_CLASS_READ_LEVEL_4 0
#define DEBUG_CLASS_READ_LEVEL_5 0
#define DEBUG_CLASS_READ_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepoSnapshot *snapshot;
    const char *id;
    size_t id_size;
    const char *name;
    size_t name_size;
    struct kndClassEntry *entry;
    struct kndClass *cls;
    struct kndClassRef *cls_ref;
    struct kndAttrRef *attr_ref;
    struct kndText *text;
    struct kndAttrStm *attr_stm;
    struct kndClass *baseclass;
    struct kndClassBasePred *base_pred;
};

static inline void base_pred_append_attr_stm(struct kndClassBasePred *bp, struct kndAttrStm *stm)
{
    if (!bp->attr_stms_tail) {
        bp->attr_stms_tail  = stm;
        bp->attr_stms = stm;
    }
    else {
        bp->attr_stms_tail->next = stm;
        bp->attr_stms_tail = stm;
    }
    bp->num_attr_stms++;
}

static gsl_err_t read_attr_stm(void *obj, const char *name, size_t name_size,
                               const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndTaskCache *cache = &task->cache;
    struct kndAttrStm *stm;
    int err;

    err = knd_attr_stm_new(&stm, ctx->base_pred->subj, cache->mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc an attr stm");
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = knd_read_attr_stm(stm, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    knd_base_pred_append_attr_stm(ctx->base_pred, stm);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_attr_stm_list(void *obj, const char *name, size_t name_size,
                                    const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndTaskCache *cache = &task->cache;
    struct kndClassBasePred *bp = ctx->base_pred;
    struct kndAttrStm *stm;
    int err;

    err = knd_attr_stm_new(&stm, bp->subj, cache->mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc an attr stm");
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = knd_read_attr_stm_list(stm, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    assert (stm->list != NULL);

    base_pred_append_attr_stm(bp, stm);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_base_cls_id(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndClassBasePred *bp = ctx->base_pred;
    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);
    memcpy(bp->id, id, id_size);
    bp->id_size = id_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_cls_ref(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndClassRef *ref = ctx->cls_ref;
    assert (ref != NULL);

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(ref->id, id, id_size);
    ref->id_size = id_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_attr_ref(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *cls = ctx->cls;
    struct kndAttrRef *ref = ctx->attr_ref;
    assert (cls != NULL);
    assert (ref != NULL);
    assert (cls->entry != NULL);

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(ref->id, id, id_size);
    ref->id_size = id_size;

    memcpy(ref->owner_id, cls->entry->id, cls->entry->id_size);
    ref->owner_id_size = cls->entry->id_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass_array_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *cls = ctx->cls;
    struct kndClassBasePred *bp;
    struct kndTask *task = ctx->task;
    struct kndTaskCache *cache = &task->cache;
    struct kndMemPool *mempool = cache->mempool;
    int err;

    err = knd_class_base_pred_new(&bp, cls, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    ctx->base_pred = bp;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_base_cls_id,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_attr_stm_list,
          .obj = ctx
        },
        { .validate = read_attr_stm,
          .obj = ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    knd_calc_num_id(bp->id, bp->id_size, &bp->numid);

    knd_class_append_base_pred(cls, bp);

    ctx->base_pred = NULL;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclasses(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    struct gslTaskSpec bp_spec = {
        .is_list_item = true,
        .parse = parse_baseclass_array_item,
        .obj = ctx
    };
    return gsl_parse_array(&bp_spec, rec, total_size);
}

static gsl_err_t parse_ancestor_array_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *cls = ctx->cls;
    struct kndTask *task = ctx->task;
    struct kndTaskCache *cache = &task->cache;
    struct kndMemPool *mempool = cache->mempool;
    struct kndClassRef *ref;
    int err;

    err = knd_class_ref_new(&ref, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    ctx->cls_ref = ref;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_cls_ref,
          .obj = obj
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    ref->next = cls->ancestors;
    cls->ancestors = ref;
    cls->num_ancestors++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_ancestor_array(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    struct gslTaskSpec bp_spec = {
        .is_list_item = true,
        .parse = parse_ancestor_array_item,
        .obj = ctx
    };
    return gsl_parse_array(&bp_spec, rec, total_size);
}

static gsl_err_t parse_attr_ref_array_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndTaskCache *cache = &task->cache;
    struct kndClass *cls = ctx->cls;
    struct kndAttrRef *ref;
    int err;

    err = knd_attr_ref_new(&ref, cache->mempool);
    if (err) return make_gsl_err_external(err);
    ctx->attr_ref = ref;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_ref,
          .obj = ctx
        }
    };
    gsl_err_t parser_err;
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    ref->next = cls->attr_refs;
    cls->attr_refs = ref;
    cls->num_attr_refs++;

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

static gsl_err_t parse_child_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *cls = ctx->cls;
    struct kndTask *task = ctx->task;
    struct kndTaskCache *cache = &task->cache;
    struct kndMemPool *mempool = cache->mempool;
    struct kndClassRef *ref;
    int err;

    err = knd_class_ref_new(&ref, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    ctx->cls_ref = ref;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_cls_ref,
          .obj = ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    ref->next = cls->children;
    cls->children = ref;
    cls->num_children++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_children_array(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec spec = {
        .is_list_item = true,
        .parse = parse_child_item,
        .obj = obj
    };
    return gsl_parse_array(&spec, rec, total_size);
}

static gsl_err_t set_descendant_ref(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndTaskCache *cache = &task->cache;
    struct kndClass *cls = ctx->cls;
    struct kndClassRef *ref;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_class_ref_new(&ref, cache->mempool);
    if (err) return make_gsl_err_external(err);

    memcpy(ref->id, id, id_size);
    ref->id_size = id_size;

    err = knd_set_add(cls->descendants, ref->id, ref->id_size, (void*)ref, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_descendant_item(void *obj, const char *rec, size_t *total_size)
{
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_descendant_ref,
          .obj = obj
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_descendant_array(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndTaskCache *cache = &task->cache;
    struct kndClass *cls = ctx->cls;
    int err;

    if (!cls->descendants) {
        err = knd_set_new(&cls->descendants, KND_SET_STORE_MEMONLY, cache->mempool);
        if (err) return *total_size = 0, make_gsl_err_external(err);
    }

    struct gslTaskSpec spec = {
        .is_list_item = true,
        .parse = parse_descendant_item,
        .obj = ctx
    };
    return gsl_parse_array(&spec, rec, total_size);
}

static gsl_err_t parse_gloss(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndTaskCache *cache = &task->cache;
    struct kndClassEntry *entry = ctx->entry;
    struct kndText *t;
    int err;

    err = knd_text_new(&t, cache->mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc a text");
        return *total_size = 0, make_gsl_err_external(err);
    }
    err = knd_gloss_parse(t, rec, total_size, task);
    if (err) {
        KND_TASK_LOG("failed to parse gloss {err %d}", err);
        return *total_size = 0, make_gsl_err_external(err);
    }

    t->next = entry->glosses;
    entry->glosses = t;

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

static gsl_err_t bp_is_root(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndTaskCache *cache = &task->cache;
    struct kndClass *cls = ctx->cls;
    struct kndClassBasePred *bp;
    int err;

    err = knd_class_base_pred_new(&bp, cls, cache->mempool);
    if (err) return make_gsl_err_external(err);

    bp->is_root = true;

    knd_calc_num_id(bp->id, bp->id_size, &bp->numid);

    knd_class_append_base_pred(cls, bp);

    return make_gsl_err(gsl_OK);
}

int knd_class_read(struct kndClass *cls, const char *rec, size_t *total_size, struct kndTask *task)
{
    if (DEBUG_CLASS_READ_LEVEL_2) {
        knd_log(".. reading {cls %.*s} GSP {rec %.*s}",
                cls->name_size, cls->name, 128, rec);
    }

    if (cls->phase >= KND_CLASS_READ) {
        knd_log("vicious circle detected while reading {cls %.*s}",
                cls->name_size, cls->name);
        return knd_FAIL;
    }

    struct LocalContext ctx = {
        .task = task,
        .cls = cls
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = knd_ignore_value,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "g",
          .name_size = strlen("g"),
          .parse = knd_ignore_named_list,
          .obj = &ctx
        },
        { .name = "is-root",
          .name_size = strlen("is-root"),
          .run = bp_is_root,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "is",
          .name_size = strlen("is"),
          .parse = parse_baseclasses,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "a",
          .name_size = strlen("a"),
          .parse = parse_attr_ref_array,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "anc",
          .name_size = strlen("anc"),
          .parse = parse_ancestor_array,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .parse = parse_children_array,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "desc",
          .name_size = strlen("desc"),
          .parse = parse_descendant_array,
          .obj = &ctx
        },
        { .name = "num-desc",
          .name_size = strlen("num-desc"),
          .parse = gsl_parse_size_t,
          .obj = &cls->num_descendants
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err.code;

    cls->phase = KND_CLASS_READ;

    return knd_OK;
}

int knd_cls_body_unmarshall(const char *unused_var(elem_id), size_t unused_var(elem_id_size),
                            const char *rec, size_t unused_var(rec_size),
                            void *ctx_obj, size_t *parsed_size, void **result, struct kndTask *task)
{
    struct kndTaskCache *cache = &task->cache;
    struct kndMemPool *mempool = cache->mempool;
    struct LocalContext *ctx = ctx_obj;
    struct kndClassEntry *entry = ctx->entry;
    struct kndClass *c;
    int err;

    assert (entry != NULL);

    if (DEBUG_CLASS_READ_LEVEL_2) {
        knd_log(".. unmarshalling cls body of {cls %.*s {rec %s}}",
                entry->id_size, entry->id, rec);
    }

    err = knd_class_new(&c, mempool);
    KND_TASK_ERR("failed to alloc a cls to unmarshall");
    c->entry = entry;
    c->name = entry->name;
    c->name_size = entry->name_size;

    err = knd_class_read(c, rec, parsed_size, task);
    KND_TASK_ERR("failed to read GSP of {cls %.*s}", c->name_size, c->name);

    *result = c;
    return knd_OK;
}

int knd_cls_name_fetch(const char *rec, size_t unused_var(rec_size),
                       const char *key, size_t key_size,
                       void *ctx_obj, size_t *result_size, void **unused_var(result),
                       struct kndTask *unused_var(task))
{
    struct LocalContext *ctx = ctx_obj;
    char namebuf[KND_NAME_SIZE];
    size_t namebuf_size = 0;
    struct kndClassRef *ref = ctx->cls_ref;
    gsl_err_t parser_err;

    assert (ref != NULL);

    memset(ref, 0, sizeof(struct kndClassRef));

    if (DEBUG_CLASS_READ_LEVEL_2) {
        knd_log(">> fetching cls entry from {rec %s}", rec);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = namebuf,
          .buf_size = &namebuf_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .name = "id",
          .name_size = strlen("id"),
          .buf = ref->id,
          .buf_size = &ref->id_size,
          .max_buf_size = KND_ID_SIZE
        }
    };

    parser_err = gsl_parse_task(rec, result_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    if (key_size != namebuf_size) return knd_NO_MATCH;
    if (memcmp(key, namebuf, namebuf_size)) return knd_NO_MATCH;

    if (DEBUG_CLASS_READ_LEVEL_3) {
        knd_log("++ {cls %.*s {id %.*s}} matched by name",
                namebuf_size, namebuf, ref->id_size, ref->id);
    }

    return knd_OK;
}

static gsl_err_t set_cls_entry_name_id(void *obj, const char *id, size_t id_size)
{
    struct kndClassEntry *entry = obj;
    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);
    memcpy(entry->name_id, id, id_size);
    entry->name_id_size = id_size;
    return make_gsl_err(gsl_OK);
}

gsl_err_t break_after_field(void *unused_var(obj), const char *unused_var(name), size_t unused_var(name_size),
                            const char *unused_var(rec), size_t *unused_var(total_size))
{
    return make_gsl_err(gsl_NO_MATCH);
}

gsl_err_t break_after_value(void *unused_var(obj), const char *unused_var(val), size_t unused_var(val_size))
{
    return make_gsl_err(gsl_NO_MATCH);
}

int knd_cls_entry_ref_unmarshall(const char *elem_id, size_t elem_id_size,
                                 const char *rec, size_t rec_size,
                                 void *unused_var(ctx_obj), size_t *total_size,
                                 void **result, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndTaskCache *cache = &task->cache;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_READ_LEVEL_2) {
        knd_log(">> unmarshall cls entry name for {id %.*s} {rec %.*s}",
                elem_id_size, elem_id, rec_size, rec);
    }

    err = knd_class_entry_new(&entry, cache->mempool);
    KND_TASK_ERR("failed to alloc a cls entry");

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_cls_entry_name_id,
          .obj = entry
        },
        { .name = "id",
          .name_size = strlen("id"),
          .buf = entry->id,
          .buf_size = &entry->id_size,
          .max_buf_size = KND_ID_SIZE
        } 
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    if (DEBUG_CLASS_READ_LEVEL_3) {
        knd_log("++ {cls-entry-ref {name-id %.*s} {id %.*s}}",
                entry->name_id_size, entry->name_id, entry->id_size, entry->id);
    }
    
    *result = entry;
    return knd_OK;
}

/*
 *  reading just the cls name id and glosses
    for concise entry representation 
*/
int knd_cls_entry_unmarshall(const char *unused_var(elem_id), size_t unused_var(elem_id_size),
                             const char *rec, size_t rec_size,
                             void *ctx_obj, size_t *total_size,
                             void **result, struct kndTask *task)
{
    struct LocalContext *ctx = ctx_obj;
    struct kndClassEntry *entry = ctx->entry;
    struct kndTaskCache *cache = &task->cache;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_READ_LEVEL_2) {
        knd_log(">> unmarshall cls name id and glosses for {cls {id %.*s}} {rec %.*s}",
                ctx->id_size, ctx->id, rec_size, rec);
    }

    if (!entry) {
        if (!ctx->id_size) {
            err = knd_FORMAT;
            KND_TASK_ERR("no cls entry id specified");
        }
        /* NB: allocation from the _cache_ pool */
        err = knd_class_entry_new(&entry, cache->mempool);
        KND_TASK_ERR("failed to alloc a cls entry");
        memcpy(entry->id, ctx->id, ctx->id_size);
        entry->id_size = ctx->id_size;
        ctx->entry = entry;
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_cls_entry_name_id,
          .obj = entry
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "g",
          .name_size = strlen("g"),
          .parse = parse_glosses,
          .obj = ctx
        }, /* ignoring all other fields */
        { .name = "is-root",
          .name_size = strlen("is-root"),
          .run = break_after_value,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = break_after_field,
          .obj = ctx
        },
        { .type = GSL_GET_STATE,
          .validate = break_after_field,
          .obj = ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    switch (parser_err.code) {
    case gsl_OK:
        break;
    case  gsl_NO_MATCH:
        break;
    default:
        return gsl_err_to_knd_err_codes(parser_err);
    }

    if (entry->name_id_size == 0) {
        err = knd_FORMAT;
        KND_TASK_ERR("failed to read cls entry name id {cls %.*s}", ctx->id_size, ctx->id);
    }

    if (DEBUG_CLASS_READ_LEVEL_3) {
        knd_log("++ {cls-entry {name-id %.*s} {id %.*s}} parsed OK!",
                entry->name_id_size, entry->name_id, entry->id_size, entry->id);
    }
    
    *result = entry;
    return knd_OK;
}


