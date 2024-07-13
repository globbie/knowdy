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
    struct kndAttrHub *attr_hub;
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
    int err;
    err = knd_read_attr_stm(ctx->base_pred, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_attr_stm_list(void *obj, const char *name, size_t name_size,
                                    const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    int err;
    err = knd_read_attr_stm_list(ctx->base_pred, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);
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
    base_pred->parent = self;
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

    if (!self->base_preds) {
        self->base_preds_tail = base_pred;
        self->base_preds = base_pred;
    } else {
        self->base_preds_tail->next = base_pred;
        self->base_preds_tail = base_pred;
    }
    self->num_base_preds++;

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

static gsl_err_t set_topic_inst_ref(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndClassInstRef *ref = ctx->class_inst_ref;
    if (DEBUG_CLASS_READ_LEVEL_2)
        knd_log("== topic inst: %.*s", name_size, name);

    ref->name = name;
    ref->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_topic_inst_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndClassRef *class_ref = ctx->class_ref;
    struct kndClassInstRef *ref;
    int err;

    err = knd_class_inst_ref_new(&ref, mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc class inst ref");
        return make_gsl_err(gsl_FAIL);
    }
    ctx->class_inst_ref = ref;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_topic_inst_ref,
          .obj = ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    ref->next = class_ref->insts;
    class_ref->insts = ref;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_topic_inst_array(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct gslTaskSpec bp_spec = {
        .is_list_item = true,
        .parse = parse_topic_inst_item,
        .obj = ctx
    };
    return gsl_parse_array(&bp_spec, rec, total_size);
}

static gsl_err_t set_attr_hub_template(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = ctx->task->user_ctx->mempool;
    struct kndRepo *repo = ctx->task->repo;
    struct kndAttrHub *hub = ctx->attr_hub;
    struct kndClassEntry *entry;
    struct kndSet *set;
    int err;
    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_shared_set_get(task->idxs->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("class \"%.*s\" not found in repo %.*s", id_size, id, repo->name_size, repo->name);
        return make_gsl_err(gsl_FAIL);
    }
    hub->topic_template = entry;

    err = knd_set_new(&set, mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc topic set for attr hub");
        return make_gsl_err(gsl_FAIL);
    }
    hub->topics = set;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_rel_topic(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndRepo *repo = task->repo;
    struct kndAttrHub *hub = ctx->attr_hub;
    struct kndClassEntry *entry;
    struct kndClassRef *ref;
    int err;
    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_shared_set_get(task->idxs->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("class \"%.*s\" not found in repo %.*s", id_size, id, repo->name_size, repo->name);
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_class_ref_new(&ref, mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc class ref");
        return make_gsl_err(gsl_FAIL);
    }
    ref->entry = entry;

    err = knd_set_add(hub->topics, id, id_size, (void*)ref);
    if (err) {
        KND_TASK_LOG("failed to register class ref");
        return make_gsl_err(gsl_FAIL);
    }
    ctx->class_ref = ref;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_rel_topic_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_rel_topic,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_i",
          .name_size = strlen("_i"),
          .parse = parse_topic_inst_array,
          .obj = ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_rel_topic_array(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct gslTaskSpec bp_spec = {
        .is_list_item = true,
        .parse = parse_rel_topic_item,
        .obj = ctx
    };
    return gsl_parse_array(&bp_spec, rec, total_size);
}

static gsl_err_t set_rel_attr(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrHub *hub = ctx->attr_hub;
    struct kndClassEntry *entry = hub->topic_template;
    struct kndClass *c;
    struct kndAttrRef *ref;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);
    hub->attr_id = id;
    hub->attr_id_size = id_size;

    err = knd_class_acquire(entry, &c, task);
    if (err) {
        KND_TASK_LOG("failed to acquire class %.*s", entry->name_size, entry->name);
        return make_gsl_err_external(err);
    }
    err = knd_set_get(c->attr_idx, id, id_size, (void**)&ref);
    if (err) {
        KND_TASK_LOG("failed to get attr %.*s in class %.*s", id_size, id, c->name_size, c->name);
        return make_gsl_err_external(err);
    }
    hub->attr = ref->attr;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_rel_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;
    struct kndMemPool *mempool = ctx->task->user_ctx->mempool;
    struct kndAttrHub *hub;
    int err;

    err = knd_attr_hub_new(mempool, &hub);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    ctx->attr_hub = hub;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_hub_template,
          .obj = ctx
        },
        { .name = "a",
          .name_size = strlen("a"),
          .run = set_rel_attr,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "tp",
          .name_size = strlen("tp"),
          .parse = parse_rel_topic_array,
          .obj = ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    hub->next = self->attr_hubs;
    self->attr_hubs = hub;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_inverse_rel_array(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    struct gslTaskSpec bp_spec = {
        .is_list_item = true,
        .parse = parse_rel_item,
        .obj = ctx
    };
    return gsl_parse_array(&bp_spec, rec, total_size);
}

static gsl_err_t read_attr(void *obj, const char *name, size_t name_size,
                           const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask    *task = ctx->task;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndClass *self = ctx->class;
    struct kndAttr *attr;
    struct kndAttrRef *ref;
    struct kndSharedSet *attr_idx = task->idxs->attr_idx;
    const char *c;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_READ_LEVEL_2) {
        knd_log(".. reading {attr %.*s} rec:\"%.*s\"", name_size, name, 32, rec);
    }

    err = knd_attr_new(&attr, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent = self;

    for (size_t i = 0; i < sizeof(knd_attr_names) / sizeof(knd_attr_names[0]); i++) {
        c = knd_attr_names[i];
        if (!memcmp(c, name, name_size)) {
            attr->type = (knd_attr_type)i;
            break;
        }
    }

    if (attr->type == KND_ATTR_NONE) {
        KND_TASK_LOG("{attr-type %.*s} is not supported in {class %.*s}",
                name_size, name, self->name_size, self->name);
        return *total_size = 0, make_gsl_err_external(err);
    }

    parser_err = knd_attr_read(attr, task, rec, total_size);
    if (parser_err.code) {
        KND_TASK_LOG("failed to read {attr %.*s}", name_size, name);
        return parser_err;
    }
    if (attr->is_implied)
        self->implied_attr = attr;

    if (!self->attr_tail) {
        self->attr_tail = attr;
        self->attrs = attr;
    } else {
        self->attr_tail->next = attr;
        self->attr_tail = attr;
    }
    self->num_attrs++;

    err = knd_shared_set_get(attr_idx, attr->id, attr->id_size, (void**)&ref);
    if (err) {
        err = knd_attr_ref_new(&ref, mempool);
        if (err) {
            KND_TASK_LOG("failed to alloc an attr ref");
            return *total_size = 0, make_gsl_err_external(err);
        }
        ref->attr = attr;

        err = knd_shared_set_add(attr_idx, attr->id, attr->id_size, (void*)ref);
        if (err) {
            KND_TASK_LOG("failed to update attr idx of {class %.*s}", self->name_size, self->name);
            return *total_size = 0, make_gsl_err_external(err);
        }
        return make_gsl_err(gsl_OK);
    }

    ref->attr = attr;

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

int knd_class_read(struct kndClass *self, const char *rec, size_t *total_size, struct kndTask *task)
{
    if (DEBUG_CLASS_READ_LEVEL_TMP) {
        knd_log(".. reading {class %.*s} GSP: \"%.*s\"",
                self->name_size, self->name, 128, rec);
    }

    if (self->phase >= KND_CLASS_READ) {
        knd_log("vicious circle detected while reading {class %.*s}",
                self->name_size, self->name);
        return knd_FAIL;
    }

    task->type = KND_UNFREEZE_STATE;

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
          .name = "rel",
          .name_size = strlen("rel"),
          .parse = parse_inverse_rel_array,
          .obj = &ctx
        },
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

    if (DEBUG_CLASS_READ_LEVEL_3) {
        knd_log(".. unmarshall {class %.*s}", entry->name_size, entry->name);
    }

    if (entry->cached_version) {
        //knd_log(">> {class %.*s} already cached", entry->name_size, entry->name);
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

    entry->cached_version = c;

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
