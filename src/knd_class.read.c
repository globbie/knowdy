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
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
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
    struct kndAttrVar *attr_var;
    struct kndAttrHub *attr_hub;
    struct kndClass *class;
    struct kndClass *baseclass;
    struct kndClassRef *class_ref;
    struct kndClassInst *class_inst;
    struct kndClassInstRef *class_inst_ref;
    struct kndClassVar *class_var;
};
static gsl_err_t read_attr_var(void *obj, const char *name, size_t name_size,
                               const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    int err;
    err = knd_read_attr_var(ctx->class_var, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_attr_var_list(void *obj, const char *name, size_t name_size,
                                    const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    int err;
    err = knd_read_attr_var_list(ctx->class_var, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_baseclass(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClassVar *class_var = ctx->class_var;
    struct kndRepo *repo = ctx->task->repo;
    struct kndClassEntry *entry;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);
    // knd_log(">> baseclass: %.*s", id_size, id);

    memcpy(class_var->id, id, id_size);
    class_var->id_size = id_size;

    err = knd_shared_set_get(repo->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("class \"%.*s\" not found in repo %.*s", id_size, id, repo->name_size, repo->name);
        return make_gsl_err(gsl_FAIL);
    }
    class_var->entry = entry;

    if (DEBUG_CLASS_READ_LEVEL_3)
        knd_log("== conc item baseclass: %.*s (id:%.*s)", entry->name_size, entry->name, id_size, id);
    
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

    err = knd_shared_set_get(repo->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("class \"%.*s\" not found in repo %.*s", id_size, id, repo->name_size, repo->name);
        return make_gsl_err(gsl_FAIL);
    }
    ref->entry = entry;

    if (DEBUG_CLASS_READ_LEVEL_3)
        knd_log("== class ref: %.*s (id:%.*s)", entry->name_size, entry->name, id_size, id);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass_array_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;
    struct kndClassVar *class_var;
    struct kndMemPool *mempool = ctx->task->user_ctx->mempool;
    int err;

    err = knd_class_var_new(mempool, &class_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    ctx->class_var = class_var;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_baseclass,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_attr_var_list,
          .obj = ctx
        },
        { .validate = read_attr_var,
          .obj = ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    knd_calc_num_id(class_var->id, class_var->id_size, &class_var->numid);

    // append
    class_var->next = self->baseclass_vars;
    self->baseclass_vars = class_var;
    self->num_baseclass_vars++;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass_array(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    struct gslTaskSpec cvar_spec = {
        .is_list_item = true,
        .parse = parse_baseclass_array_item,
        .obj = ctx
    };
    return gsl_parse_array(&cvar_spec, rec, total_size);
}

static gsl_err_t parse_ancestor_array_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;
    struct kndMemPool *mempool = ctx->task->user_ctx->mempool;
    struct kndClassRef *ref;
    int err;

    err = knd_class_ref_new(mempool, &ref);
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

    struct gslTaskSpec cvar_spec = {
        .is_list_item = true,
        .parse = parse_ancestor_array_item,
        .obj = ctx
    };
    return gsl_parse_array(&cvar_spec, rec, total_size);
}

static gsl_err_t parse_child_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;
    struct kndMemPool *mempool = ctx->task->user_ctx->mempool;
    struct kndClassRef *ref;
    int err;

    err = knd_class_ref_new(mempool, &ref);
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

    struct gslTaskSpec cvar_spec = {
        .is_list_item = true,
        .parse = parse_child_item,
        .obj = ctx
    };
    return gsl_parse_array(&cvar_spec, rec, total_size);
}

static gsl_err_t set_topic_inst_ref(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndClassInstRef *ref = ctx->class_inst_ref;
    if (DEBUG_CLASS_READ_LEVEL_TMP)
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

    err = knd_class_inst_ref_new(mempool, &ref);
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
    struct gslTaskSpec cvar_spec = {
        .is_list_item = true,
        .parse = parse_topic_inst_item,
        .obj = ctx
    };
    return gsl_parse_array(&cvar_spec, rec, total_size);
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

    err = knd_shared_set_get(repo->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("class \"%.*s\" not found in repo %.*s", id_size, id, repo->name_size, repo->name);
        return make_gsl_err(gsl_FAIL);
    }
    hub->topic_template = entry;

    err = knd_set_new(mempool, &set);
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

    err = knd_shared_set_get(repo->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("class \"%.*s\" not found in repo %.*s", id_size, id, repo->name_size, repo->name);
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_class_ref_new(mempool, &ref);
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
    struct gslTaskSpec cvar_spec = {
        .is_list_item = true,
        .parse = parse_rel_topic_item,
        .obj = ctx
    };
    return gsl_parse_array(&cvar_spec, rec, total_size);
}

static gsl_err_t set_rel_attr(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrHub *hub = ctx->attr_hub;
    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);
    hub->attr_id = id;
    hub->attr_id_size = id_size;
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

    struct gslTaskSpec cvar_spec = {
        .is_list_item = true,
        .parse = parse_rel_item,
        .obj = ctx
    };
    return gsl_parse_array(&cvar_spec, rec, total_size);
}

static gsl_err_t check_class_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx      = obj;
    struct kndRepo *repo          = ctx->repo;
    // struct kndClassEntry *entry = NULL;

    if (DEBUG_CLASS_READ_LEVEL_2)
        knd_log(".. repo \"%.*s\" to check a class name: \"%.*s\" (size:%zu)",
                repo->name_size, repo->name, name_size, name, name_size);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_attr(void *obj, const char *name, size_t name_size, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask    *task = ctx->task;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndClass *self = ctx->class;
    struct kndAttr *attr;
    struct kndAttrRef *ref;
    struct kndSet     *attr_idx = self->attr_idx;
    const char *c;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_READ_LEVEL_2)
        knd_log(".. reading attr: \"%.*s\" rec:\"%.*s\"", name_size, name, 32, rec);

    err = knd_attr_new(mempool, &attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;

    for (size_t i = 0; i < sizeof(knd_attr_names) / sizeof(knd_attr_names[0]); i++) {
        c = knd_attr_names[i];
        if (!memcmp(c, name, name_size)) 
            attr->type = (knd_attr_type)i;
    }

    if (attr->type == KND_ATTR_NONE) {
        KND_TASK_LOG("\"%.*s\" attr is not supported (class \"%.*s\")",
                name_size, name, self->name_size, self->name);
        return *total_size = 0, make_gsl_err_external(err);
    }
    parser_err = knd_attr_read(attr, task, rec, total_size);
    if (parser_err.code) {
        KND_TASK_LOG("failed to read attr \"%.*s\"", name_size, name);
        return parser_err;
    }
    if (attr->is_implied)
        self->implied_attr = attr;

    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    } else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }
    self->num_attrs++;

    /* new attr entry */
    err = knd_attr_ref_new(mempool, &ref);
    if (err) {
        KND_TASK_LOG("failed to alloc an attr ref");
        return *total_size = 0, make_gsl_err_external(err);
    }
    ref->attr = attr;

    err = attr_idx->add(attr_idx, attr->id, attr->id_size, (void*)ref);
    if (err) {
        KND_TASK_LOG("failed to update attr idx of %.*s", self->name_size, self->name);
        return *total_size = 0, make_gsl_err_external(err);
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
    if (DEBUG_CLASS_READ_LEVEL_2)
        knd_log(".. reading class GSP: \"%.*s\"", 128, rec);

    if (self->resolving_in_progress) {
        knd_log("vicious circle detected while reading class \"%.*s\"",
                self->name_size, self->name);
        return knd_FAIL;
    }
    self->resolving_in_progress = true;

    task->type = KND_UNFREEZE_STATE;

    struct LocalContext ctx = {
        .task = task,
        .class = self,
        .repo = task->repo
    };

    /*struct gslTaskSpec inst_commit_spec = {
        .is_list_item = true,
        .parse  = parse_class_inst_item,
        .obj = &ctx
    };
    */
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_class_name,
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
    return knd_OK;
}

static int inherit_attr(struct kndClass *self, struct kndAttr *attr, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndSet     *attr_idx = self->attr_idx;
    struct kndAttrRef *ref = NULL;
    int err;

    if (DEBUG_CLASS_READ_LEVEL_3)
        knd_log("..  \"%.*s\" (id:%.*s size:%zu) attr of \"%.*s\" to be inherited by %.*s",
                attr->name_size, attr->name, attr->id_size, attr->id, attr->id_size,
                attr->parent_class->name_size, attr->parent_class->name,
                self->name_size, self->name);

    err = knd_attr_ref_new(mempool, &ref);
    KND_TASK_ERR("failed to alloc an attr ref");
    ref->attr = attr;
    ref->class_entry = attr->parent_class->entry;

    err = knd_set_add(attr_idx, attr->id, attr->id_size, (void*)ref);
    KND_TASK_ERR("failed to update attr idx of %.*s", self->name_size, self->name);

    return knd_OK;
}

static int resolve_class(struct kndClass *self, struct kndTask *task)
{
    struct kndClassRef *ref;
    struct kndClassEntry *entry;
    struct kndClass *c;
    struct kndAttr *attr;
    int err;

    FOREACH (ref, self->ancestors) {
        entry = ref->entry;
        err = knd_class_acquire(entry, &c, task);
        KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);

        if (DEBUG_CLASS_READ_LEVEL_2)
            knd_log(".. class \"%.*s\" to inherit attrs from \"%.*s\"",
                    self->name_size, self->name, c->name_size, c->name);

        FOREACH (attr, c->attrs) {
            err = inherit_attr(self, attr, task);
            KND_TASK_ERR("class \"%.*s\" failed to inherit attr %.*s from \"%.*s\"",
                         self->name_size, self->name,
                         attr->name_size, attr->name, c->name_size, c->name);
        }
        ref->class = c;
    }
    return knd_OK;
}

int knd_class_acquire(struct kndClassEntry *entry, struct kndClass **result, struct kndTask *task)
{
    struct kndRepo *repo = entry->repo;
    struct kndClass *c = NULL, *prev_c = NULL;
    // int num_readers;
    int err;

    if (DEBUG_CLASS_READ_LEVEL_3)
        knd_log(">> acquire class \"%.*s\"", entry->name_size, entry->name);

    // TODO read/write conflicts
    atomic_fetch_add_explicit(&entry->num_readers, 1, memory_order_relaxed);
 
    do {
        prev_c = atomic_load_explicit(&entry->class, memory_order_relaxed);
        if (prev_c) {
            // TODO if (c != NULL)  - free
            if (DEBUG_CLASS_READ_LEVEL_3)
                knd_log("++ %.*s class is already cached (class:%p)",
                        entry->name_size, entry->name, prev_c);
            *result = prev_c;
            return knd_OK;
        }
        if (!c) {
            err = knd_shared_set_unmarshall_elem(repo->class_idx, entry->id, entry->id_size,
                                                 knd_class_unmarshall, (void**)&c, task);
            if (err) return err;
            c->entry = entry;
            // entry->class = c;
            c->name = entry->name;
            c->name_size = entry->name_size;

            err = resolve_class(c, task);
            KND_TASK_ERR("failed to resolve class %.*s", c->name_size, c->name);
        }
    } while (!atomic_compare_exchange_weak(&entry->class, &prev_c, c));

    *result = c;
    return knd_OK;
}

int knd_class_unmarshall(const char *unused_var(elem_id), size_t unused_var(elem_id_size),
                         const char *rec, size_t rec_size,
                         void **result, struct kndTask *task)
{
    struct kndClass *c = NULL;
    size_t total_size = rec_size;
    int err;

    if (DEBUG_CLASS_READ_LEVEL_TMP)
        knd_log(">> GSP rec: \"%.*s\"", rec_size, rec);

    err = knd_class_new(task->user_ctx->mempool, &c);
    KND_TASK_ERR("failed to alloc a class");

    err = knd_class_read(c, rec, &total_size, task);
    KND_TASK_ERR("failed to read GSP class rec");
    
    *result = c;
    return knd_OK;
}
