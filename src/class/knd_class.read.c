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
    struct kndRepo *repo;
    struct kndAttrStm *attr_stm;
    struct kndClassEntry *entry;
    struct kndClass *class;
    struct kndClass *baseclass;
    struct kndClassRef *class_ref;
    struct kndClassInst *class_inst;
    struct kndClassInstRef *class_inst_ref;
    struct kndClassBasePred *base_pred;
};

static gsl_err_t read_attr_stm(void *obj, const char *name, size_t name_size,
                               const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrStm *stm;
    int err;

    err = knd_attr_stm_new(&stm, task->mempool);
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
    struct kndAttrStm *stm;
    int err;

    err = knd_attr_stm_new(&stm, task->mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc an attr stm");
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = knd_read_attr_stm_list(stm, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    assert (stm->list != NULL);

    knd_base_pred_append_attr_stm(ctx->base_pred, stm);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_baseclass(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClassBasePred *base_pred = ctx->base_pred;
    struct kndRepo *repo = ctx->task->repo;
    struct kndClassEntry *entry;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(base_pred->id, id, id_size);
    base_pred->id_size = id_size;

    err = knd_shared_set_get(task->idxs->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("{class %.*s} not found in {repo %.*s}",
                     id_size, id, repo->name_size, repo->name);
        return make_gsl_err(gsl_FAIL);
    }
    base_pred->entry = entry;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_class_ref(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClassRef *ref = ctx->class_ref;
    struct kndRepo *repo = ctx->task->repo;
    struct kndClassEntry *entry;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_shared_set_get(task->idxs->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("{class %.*s} not found in {repo %.*s}",
                     id_size, id, repo->name_size, repo->name);
        return make_gsl_err(gsl_FAIL);
    }
    ref->entry = entry;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass_array_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;
    struct kndClassBasePred *base_pred;
    struct kndMemPool *mempool = ctx->task->mempool;
    int err;

    err = knd_class_base_pred_new(&base_pred, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    base_pred->owner = self;
    ctx->base_pred = base_pred;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_baseclass,
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

    knd_calc_num_id(base_pred->id, base_pred->id_size, &base_pred->numid);

    knd_class_append_base_pred(self, base_pred);

    ctx->base_pred = NULL;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_class_name(void *obj, const char *name, size_t name_size)
{
    struct kndClassEntry *entry = obj;

    entry->name = name;
    entry->name_size = name_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_entry_array_item(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndMemPool *mempool = task->mempool;
    struct kndSharedDict *class_name_idx = task->idxs->class_name_idx;
    struct kndSharedSet *class_idx = task->idxs->class_idx;
    struct kndRepoSnapshot *snapshot = task->snapshot;
    struct kndMemBlock *memblock;
    struct kndClassEntry *entry;
    const char *b;
    size_t num_requests = 0;
    int err;

    err = knd_class_entry_new(&entry, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_name,
          .obj = entry
        },
        { .name = "id",
          .name_size = strlen("id"),
          .buf = entry->id,
          .buf_size = &entry->id_size,
          .max_buf_size = KND_ID_SIZE
        },
        { .name = "freq",
          .name_size = strlen("freq"),
          .parse = gsl_parse_size_t,
          .obj = &num_requests
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    err = knd_repo_snapshot_fetch_memblock(snapshot, entry->name_size, &memblock, task);
    if (err) {
        KND_TASK_LOG("failed to fetch a memblock to save entry name %.*s",
                     entry->name_size, entry->name);
        return make_gsl_err_external(err);
    }

    err = knd_memblock_write(memblock, entry->name, entry->name_size, &b);
    if (err) {
        KND_TASK_LOG("failed to to save entry name %.*s",
                     entry->name_size, entry->name);
        return make_gsl_err_external(err);
    }

    entry->name = b;
    knd_calc_num_id(entry->id, entry->id_size, &entry->numid);

    if (num_requests) {
        atomic_store_explicit(&entry->num_requests, num_requests, memory_order_relaxed);
    }

    if (DEBUG_CLASS_READ_LEVEL_3) {
        knd_log(".. register class entry %.*s", entry->name_size, entry->name);
    }

    err = knd_shared_dict_set(class_name_idx, entry->name, entry->name_size, (void*)entry);
    if (err) {
        KND_TASK_LOG("entry %.*s already registered?", entry->name_size, entry->name);
        return make_gsl_err_external(err);
    }

    err = knd_shared_set_add(class_idx, entry->id, entry->id_size, (void*)entry);
    if (err) {
        KND_TASK_LOG("{class-entry %.*s already registered}?",
                     entry->id_size, entry->id);
        return make_gsl_err_external(err);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_entry_array(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    struct gslTaskSpec bp_spec = {
        .is_list_item = true,
        .parse = parse_class_entry_array_item,
        .obj = ctx
    };
    return gsl_parse_array(&bp_spec, rec, total_size);
}

static gsl_err_t parse_baseclass_array(void *obj, const char *rec, size_t *total_size)
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
    struct kndClass *self = ctx->class;
    struct kndMemPool *mempool = ctx->task->user_ctx->mempool;
    struct kndClassRef *ref;
    int err;

    err = knd_class_ref_new(&ref, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    ctx->class_ref = ref;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_ref,
          .obj = ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    // append
    ref->next = self->ancestors;
    self->ancestors = ref;
    self->num_ancestors++;
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

static gsl_err_t parse_child_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;
    struct kndMemPool *mempool = ctx->task->user_ctx->mempool;
    struct kndClassRef *ref;
    int err;

    err = knd_class_ref_new(&ref, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    ctx->class_ref = ref;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_ref,
          .obj = ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    // append
    ref->next = self->children;
    self->children = ref;
    self->num_children++;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_children_array(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    struct gslTaskSpec bp_spec = {
        .is_list_item = true,
        .parse = parse_child_item,
        .obj = ctx
    };
    return gsl_parse_array(&bp_spec, rec, total_size);
}

static gsl_err_t set_descendant_ref(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->task->repo;
    struct kndClass *c = ctx->class;
    struct kndClassEntry *entry;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_shared_set_get(task->idxs->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("{class %.*s} not found in {repo %.*s}",
                     id_size, id, repo->name_size, repo->name);
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_set_add(c->descendants, entry->id, entry->id_size, (void*)entry);    
    if (err) {
        KND_TASK_LOG("failed to add descendant ref {class %.*s}",
                     entry->id_size, entry->id);
        return make_gsl_err(gsl_FAIL);
    }
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
    struct kndMemPool *mempool = ctx->task->mempool;
    struct kndClass *c = ctx->class;
    int err;

    if (!c->descendants) {
        err = knd_set_new(&c->descendants, mempool);
        if (err) return *total_size = 0, make_gsl_err_external(err);
    }

    struct gslTaskSpec bp_spec = {
        .is_list_item = true,
        .parse = parse_descendant_item,
        .obj = ctx
    };
    return gsl_parse_array(&bp_spec, rec, total_size);
}

static int update_attr_idx_cache(struct kndAttr *attr, struct kndTask *task)
{
    struct kndSharedSet *attr_idx = task->idxs->attr_idx;
    struct kndAttrRef *ref;
    int err;

    err = knd_shared_set_get(attr_idx, attr->id, attr->id_size, (void**)&ref);
    if (err) {
        knd_log("no such {attr %.*s} in attr idx", attr->name_size, attr->name);
        return knd_CONFLICT;
    }

    ref->attr = attr;
    return knd_OK;
}

static gsl_err_t read_attr(void *obj, const char *name, size_t name_size,
                           const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask    *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndClass *self = ctx->class;
    struct kndAttr *attr;
    struct kndQuantAttr *quant_attr;
    struct kndClassRefAttr *cls_ref_attr;
    struct kndClassInnerAttr *cls_inner_attr;
    const char *c;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_READ_LEVEL_2) {
        knd_log(".. reading {attr %.*s} rec:\"%.*s\"", name_size, name, 32, rec);
    }

    err = knd_attr_new(&attr, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->owner = self;

    for (size_t i = 0; i < sizeof(knd_attr_names) / sizeof(knd_attr_names[0]); i++) {
        c = knd_attr_names[i];
        if (!memcmp(c, name, name_size)) {
            attr->type = (knd_attr_type)i;
            break;
        }
    }

    switch (attr->type) {
    case KND_ATTR_NONE:
        knd_log("{attr-type %.*s} is not supported for {class %.*s}",
                name_size, name, self->name_size, self->name);
        return make_gsl_err_external(knd_NO_MATCH);
    case KND_ATTR_UINT:
        err = knd_quant_attr_new(&quant_attr, KND_QUANT_UINT, name, name_size, mempool);
        if (err) {
            return make_gsl_err_external(err);
        }
        attr->subtype = quant_attr;
        break;
    case KND_ATTR_UREAL:
        err = knd_quant_attr_new(&quant_attr, KND_QUANT_UREAL, name, name_size, mempool);
        if (err) {
            return make_gsl_err_external(err);
        }
        attr->subtype = quant_attr;
        break;
    case KND_ATTR_CLS_INNER:
        err = knd_cls_inner_attr_new(&cls_inner_attr, name, name_size, mempool);
        if (err) {
            return make_gsl_err_external(err);
        }
        attr->subtype = cls_inner_attr;
        break;
    case KND_ATTR_CLS_REF:
        err = knd_cls_ref_attr_new(&cls_ref_attr, name, name_size, mempool);
        if (err) {
            return make_gsl_err_external(err);
        }
        attr->subtype = cls_ref_attr;
        break;
    default:
        break;
    }

    parser_err = knd_attr_read(attr, task, rec, total_size);
    if (parser_err.code) {
        KND_TASK_LOG("failed to read {attr %.*s}", name_size, name);
        return parser_err;
    }

    knd_class_append_attr(self, attr);

    switch (task->type) {
    case KND_READ_SNAPSHOT_STATE:
        // fall through
    case KND_BUILD_SNAPSHOT_STATE:
        err = update_attr_idx_cache(attr, task);
        if (err) {
            KND_TASK_LOG("failed to update attr idx cache with {attr %.*s}",
                         attr->name_size, attr->name);
            return *total_size = 0, make_gsl_err_external(err);
        }
        break;
    default:
        break;
    }

    if (DEBUG_CLASS_READ_LEVEL_2) {
        knd_log("++ assigned {class %.*s {attr %.*s {id %.*s}}}",
                self->name_size, self->name,
                attr->name_size, attr->name, attr->id_size, attr->id);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_glosses(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClass *self = ctx->class;
    gsl_err_t parser_err;

    parser_err = knd_read_gloss_array((void*)task, rec, total_size);
    if (parser_err.code) return *total_size = 0, parser_err;

    if (task->ctx->tr) {
        self->tr = task->ctx->tr;
        task->ctx->tr = NULL;
    }
    return make_gsl_err(gsl_OK);
}

int knd_class_read(struct kndClass *self, const char *rec, size_t *total_size,
                   struct kndTask *task)
{
    if (DEBUG_CLASS_READ_LEVEL_2) {
        knd_log(".. reading {class %.*s} GSP: \"%.*s\"",
                self->name_size, self->name, 128, rec);
    }

    if (self->phase >= KND_CLASS_READ) {
        knd_log("vicious circle detected while reading {class %.*s}",
                self->name_size, self->name);
        return knd_FAIL;
    }

    struct LocalContext ctx = {
        .task = task,
        .class = self,
        .repo = task->repo
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = knd_ignore_value,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "g",
          .name_size = strlen("g"),
          .parse = read_glosses,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "is",
          .name_size = strlen("is"),
          .parse = parse_baseclass_array,
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
          .obj = &self->num_descendants
        }/*,
        { .type = GSL_GET_ARRAY_STATE,
          .name = "rel",
          .name_size = strlen("rel"),
          .parse = parse_inverse_rel_array,
          .obj = &ctx
          }*/,
        { .validate = read_attr,
          .obj = &ctx
        },
        { .name = "insts",
          .name_size = strlen("insts"),
          .parse = gsl_parse_size_t,
          .obj = &self->num_snapshot_insts
        },
        /*,
        { .type = GSL_GET_ARRAY_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = gsl_parse_array,
          .obj = &inst_commit_spec
        }*/
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err.code;

    self->phase = KND_CLASS_READ;
    return knd_OK;
}

int knd_class_unmarshall(const char *unused_var(elem_id), size_t unused_var(elem_id_size),
                         const char *rec, size_t rec_size,
                         void *ctx, void **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClassEntry *entry = ctx;
    struct kndClass *c;
    size_t total_size = rec_size;
    int err;

    if (DEBUG_CLASS_READ_LEVEL_2) {
        knd_log(".. unmarshall {class %.*s} {task {type %d}}",
                entry->name_size, entry->name, task->type);
    }

    if (entry->cached_version) {
        *result = entry->cached_version;
        return knd_OK;
    }

    err = knd_class_new(&c, mempool);
    KND_TASK_ERR("failed to alloc a class to unmarshall");
    c->entry = entry;
    c->name = entry->name;
    c->name_size = entry->name_size;

    err = knd_class_read(c, rec, &total_size, task);
    KND_TASK_ERR("failed to read GSP of %.*s", c->name_size, c->name);

    switch (task->type) {
    case KND_READ_SNAPSHOT_STATE:
        // fall through
    case KND_BUILD_SNAPSHOT_STATE:
        entry->cached_version = c;
        break;
    default:
        // TODO update task local cache?


        break;
    }

    *result = c;
    return knd_OK;
}

int knd_class_names_unmarshall(const char *unused_var(elem_id), size_t unused_var(elem_id_size),
                               const char *rec, size_t rec_size, struct kndTask *task)
{
    size_t total_size = rec_size;

    if (DEBUG_CLASS_READ_LEVEL_3) {
        knd_log(">> class names block:  %.*s", rec_size, rec);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = knd_ignore_value,
          .obj = task
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .parse = parse_class_entry_array,
          .obj = task
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, &total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err.code;

    return knd_OK;
}
