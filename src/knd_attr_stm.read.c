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
#include "knd_attr_stm.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#define DEBUG_ATTR_STM_READ_LEVEL_1 0
#define DEBUG_ATTR_STM_READ_LEVEL_2 0
#define DEBUG_ATTR_STM_READ_LEVEL_3 0
#define DEBUG_ATTR_STM_READ_LEVEL_4 0
#define DEBUG_ATTR_STM_READ_LEVEL_5 0
#define DEBUG_ATTR_STM_READ_LEVEL_TMP 1

struct LocalContext {
    struct kndClassBasePred *base_pred;
    struct kndClass    *class;
    struct kndClass    *inner_class;
    struct kndAttrStm  *list_parent;
    struct kndSet      *attr_idx;
    struct kndAttr     *attr;
    struct kndAttrStm  *attr_stm;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

static gsl_err_t read_attr_stm_list_item(void *obj, const char *rec, size_t *total_size);
static gsl_err_t read_nested_attr_stm(void *obj, const char *name, size_t name_size,
                                      const char *rec, size_t *total_size);
static gsl_err_t set_attr_stm_val_id(void *obj, const char *val, size_t val_size);
static gsl_err_t confirm_attr_stm(void *obj, const char *unused_var(name), size_t unused_var(name_size));

static void append_attr_stm(struct kndClassBasePred *ci, struct kndAttrStm *attr_stm)
{
    if (!ci->tail) {
        ci->tail  = attr_stm;
        ci->attr_stms = attr_stm;
    }
    else {
        ci->tail->next = attr_stm;
        ci->tail = attr_stm;
    }
    ci->num_attrs++;
}

static gsl_err_t parse_text(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndText *text;
    gsl_err_t parser_err;
    int err;

    err = knd_text_new(mempool, &text);
    if (err) return *total_size = 0, make_gsl_err_external(knd_NOMEM);

    parser_err = knd_text_read(text, rec, total_size, task);
    if (parser_err.code) {
        KND_TASK_LOG("text read failed");
        return parser_err;
    }
    ctx->attr_stm->text = text;
    text->attr_stm = ctx->attr_stm;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_proc_ref(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrStm *self = ctx->attr_stm;
    // int err;

    if (DEBUG_ATTR_STM_READ_LEVEL_3)
        knd_log(".. set proc ref: \"%.*s\" => \"%.*s\"",
                self->attr->name_size, self->attr->name, val_size, val);

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    self->val = val;
    self->val_size = val_size;
    // TODO resolve ref
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_ref(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_proc_ref,
          .obj = ctx
        }        
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_nested_attr_stm_list(void *obj, const char *id, size_t id_size,
                                           const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndAttrStm *parent_attr_stm = ctx->attr_stm;
    struct kndAttrStm *attr_stm;
    struct kndAttrRef *ref;
    struct kndAttr *attr;
    int err;

    if (DEBUG_ATTR_STM_READ_LEVEL_2) {
        knd_log(".. reading nested attr_stm list: \"%.*s\" REC: %.*s",
                id_size, id, 32, rec);
    }
    assert(ctx->class != NULL);

    err = knd_set_get(ctx->class->attr_idx, id, id_size, (void**)&ref);
    if (err) {
        KND_TASK_LOG("{class %.*s} has no list attr: %.*s",
                     ctx->class->name_size, ctx->class->name, id_size, id);
        return *total_size = 0, make_gsl_err_external(err);
    }

    assert(ref->attr != NULL);
    attr = ref->attr;

    if (DEBUG_ATTR_STM_READ_LEVEL_2) {
        knd_log(">> list attr decoded: %.*s  (type:%d)",
                attr->name_size, attr->name, attr->type);
    }
    err = knd_attr_stm_new(&attr_stm, mempool);
    if (err) return make_gsl_err(err);
    attr_stm->attr = attr;
    attr_stm->name = attr->name;
    attr_stm->name_size = attr->name_size;

    attr_stm->next = parent_attr_stm->children;
    parent_attr_stm->children = attr_stm;
    parent_attr_stm->num_children++;

    struct LocalContext attr_stm_ctx = {
        .list_parent = attr_stm,
        .task = task
    };
    struct gslTaskSpec read_attr_stm_spec = {
        .is_list_item = true,
        .parse = read_attr_stm_list_item,
        .obj = &attr_stm_ctx
    };

    switch (attr->type) {
    case KND_ATTR_INNER:
        assert(attr->class_entry != NULL);

        err = knd_class_acquire(attr->class_entry, &attr->cls, task);
        if (err) {
            KND_TASK_LOG("failed to acquire {class %.*s}",
                         attr->class_entry->name_size, attr->class_entry->name);
            return *total_size = 0, make_gsl_err_external(err);
        }
        if (DEBUG_ATTR_STM_READ_LEVEL_2) {
            knd_log(">> list inner {class %.*s}",
                    attr->class_entry->name_size, attr->class_entry->name);
        }
        break;
    default:
        break;
    }
    return gsl_parse_array(&read_attr_stm_spec, rec, total_size);
}

static gsl_err_t read_nested_attr_stm(void *obj, const char *id, size_t id_size,
                                      const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrStm *self = ctx->attr_stm;
    struct kndTask    *task = ctx->task;
    struct kndAttrStm *var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_attr_stm_new(&var, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    var->parent = self;

    memcpy(var->id, id, id_size);
    var->id_size = id_size;

    struct LocalContext attr_stm_ctx = {
        .attr_stm = var,
        .task = task
    };

    if (DEBUG_ATTR_STM_READ_LEVEL_2) {
        knd_log(".. read nested attr {var %.*s} {parent %.*s}",
                var->name_size, var->name, self->name_size, self->name);
    }
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_stm_val_id,
          .obj = &attr_stm_ctx
        },
        { .name = "_t",
          .name_size = strlen("_t"),
          .parse = parse_text,
          .obj = &attr_stm_ctx
        },
        { .name = "_p",
          .name_size = strlen("_p"),
          .parse = parse_proc_ref,
          .obj = &attr_stm_ctx
        },
        { .validate = read_nested_attr_stm,
          .obj = &attr_stm_ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_nested_attr_stm_list,
          .obj = &attr_stm_ctx
        },
        { .is_default = true,
          .run = confirm_attr_stm,
          .obj = var
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        KND_TASK_LOG("attr var reading failed: %d", parser_err.code);
        return parser_err;
    }

    var->next = self->children;
    self->children = var;
    self->num_children++;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_attr_stm_id(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrStm *var = ctx->attr_stm;
    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);
    
    memcpy(var->id, id, id_size);
    var->id_size = id_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_attr_stm_val_id(void *obj, const char *val_id, size_t val_id_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrStm *self = ctx->attr_stm;

    if (!val_id_size) return make_gsl_err(gsl_FORMAT);
    if (val_id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);
    
    memcpy(self->val_id, val_id, val_id_size);
    self->val_id_size = val_id_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr_stm(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttrStm *attr_stm = obj;

    // TODO empty values?

    if (DEBUG_ATTR_STM_READ_LEVEL_2) {
        if (!attr_stm->val_size)
            knd_log("NB: attr var value not set in %.*s (class: %.*s)",
                    attr_stm->name_size, attr_stm->name,
                    attr_stm->base_pred->entry->name_size,
                    attr_stm->base_pred->entry->name);
    }
    return make_gsl_err(gsl_OK);
}


static gsl_err_t append_attr_stm_list_item(void *accu, void *obj)
{
    struct kndAttrStm *self = accu;
    struct kndAttrStm *attr_stm = obj;

    if (!self->list_tail) {
        self->list_tail = attr_stm;
        self->list = attr_stm;
    }
    else {
        self->list_tail->next = attr_stm;
        self->list_tail = attr_stm;
    }
    self->num_list_elems++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_attr_stm_list_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrStm *self = ctx->list_parent;
    struct kndAttrStm *attr_stm, *prev_attr_stm;
    struct kndMemPool *mempool = task->mempool;
    int err;

    err = knd_attr_stm_new(&attr_stm, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_stm->is_list_item = true;
    attr_stm->parent = self;

    prev_attr_stm = ctx->attr_stm;
    ctx->attr_stm = attr_stm;

    if (DEBUG_ATTR_STM_READ_LEVEL_2) {
        knd_log("== reading a list of {attr-stm %.*s}: %.*s",
                self->name_size, self->name, 32, rec);
    }
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_stm_id,
          .obj = ctx
        },
        { .validate = read_nested_attr_stm,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_nested_attr_stm_list,
          .obj = ctx
        },
        { .is_default = true,
          .run = confirm_attr_stm,
          .obj = attr_stm
        }        
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        return parser_err;
    }
    ctx->attr_stm = prev_attr_stm;

    // append
    return append_attr_stm_list_item(self, attr_stm);
}

int knd_read_attr_stm_list(struct kndClassBasePred *self, const char *id, size_t id_size,
                           const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndAttrStm *stm;
    gsl_err_t parser_err;
    int err;

    if (id_size > KND_ID_SIZE) return knd_LIMIT;

    err = knd_attr_stm_new(&stm, mempool);
    KND_TASK_ERR("failed to alloc an attr stm");
    stm->base_pred = self;

    memcpy(stm->id, id, id_size);
    stm->id_size = id_size;

    struct LocalContext ctx = {
        .list_parent = stm,
        .task = task
    };

    struct gslTaskSpec read_attr_stm_spec = {
        .is_list_item = true,
        .parse = read_attr_stm_list_item,
        .obj = &ctx
    };
    parser_err = gsl_parse_array(&read_attr_stm_spec, rec, total_size);
    if (parser_err.code) return parser_err.code;

    append_attr_stm(self, stm);
    return knd_OK;
}

int knd_read_attr_stm(struct kndClassBasePred *self, const char *id, size_t id_size,
                      const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndAttrStm *stm;
    gsl_err_t parser_err;
    int err;

    if (id_size > KND_ID_SIZE) return knd_LIMIT;

    err = knd_attr_stm_new(&stm, mempool);
    KND_TASK_ERR("failed to alloc an attr stm");
    stm->base_pred = self;

    memcpy(stm->id, id, id_size);
    stm->id_size = id_size;

    struct LocalContext ctx = {
        .attr_stm = stm,
        .task = task
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_stm_val_id,
          .obj = &ctx
        },
        { .name = "_t",
          .name_size = strlen("_t"),
          .parse = parse_text,
          .obj = &ctx
        },
        { .name = "_p",
          .name_size = strlen("_p"),
          .parse = parse_proc_ref,
          .obj = &ctx
        },
        { .validate = read_nested_attr_stm,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_nested_attr_stm_list,
          .obj = &ctx
        },
        { .is_default = true,
          .run = confirm_attr_stm,
          .obj = stm
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- attr stm parsing failed: %d", parser_err.code);
        return parser_err.code;
    }
    append_attr_stm(self, stm);

    return knd_OK;
}
