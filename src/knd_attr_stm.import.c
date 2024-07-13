#include "knd_attr.h"

#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#include <assert.h>
#include <string.h>

#include "knd_task.h"
#include "knd_user.h"
#include "knd_class.h"
#include "knd_attr_stm.h"
#include "knd_proc.h"
#include "knd_text.h"
#include "knd_logic.h"
#include "knd_output.h"

#define DEBUG_ATTR_STM_LEVEL_1 0
#define DEBUG_ATTR_STM_LEVEL_2 0
#define DEBUG_ATTR_STM_LEVEL_3 0
#define DEBUG_ATTR_STM_LEVEL_4 0
#define DEBUG_ATTR_STM_LEVEL_5 0
#define DEBUG_ATTR_STM_LEVEL_TMP 1

struct LocalContext {
    struct kndClassBasePred *base_pred;
    struct kndAttrStm  *list_parent;
    struct kndAttr     *attr;
    struct kndAttrStm  *attr_stm;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

static gsl_err_t import_attr_stm_list_item(void *obj, const char *rec, size_t *total_size);

static gsl_err_t import_nested_attr_stm(void *obj, const char *name, size_t name_size,
                                        const char *rec, size_t *total_size);
static void append_attr_stm(struct kndClassBasePred *ci, struct kndAttrStm *attr_stm);

static gsl_err_t set_attr_stm_name(void *obj, const char *name, size_t name_size)
{
    struct kndAttrStm *self = obj;

    if (DEBUG_ATTR_STM_LEVEL_2)
        knd_log(".. set attr var name: %.*s is_list_item:%d val:%.*s",
                name_size, name, self->is_list_item,
                self->val_size, self->val);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_class_inst_ref(void *obj, const char *name, size_t name_size)
{
    struct kndAttrStm *self = obj;

    if (DEBUG_ATTR_STM_LEVEL_2)
        knd_log(">> attr var {%.*s} to set {class %.*s {inst  %.*s}}",
                self->name_size, self->name, self->val_size, self->val,
                name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->class_inst_name = name;
    self->class_inst_name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_attr_stm_value(void *obj, const char *val, size_t val_size)
{
    struct kndAttrStm *self = obj;
    if (DEBUG_ATTR_STM_LEVEL_3)
        knd_log(".. set attr var value: \"%.*s\" => \"%.*s\"",
                self->name_size, self->name, val_size, val);

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    self->val = val;
    self->val_size = val_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr_stm(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrStm *attr_stm = ctx->attr_stm;

    KND_TASK_LOG("NB: attr var value not set in \"%.*s\"",
                 attr_stm->name_size, attr_stm->name);

    return make_gsl_err(gsl_FORMAT);
}

static gsl_err_t import_nested_attr_stm_list(void *obj, const char *name, size_t name_size,
                                             const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndAttrStm *parent_attr_stm = ctx->attr_stm;
    struct kndAttrStm *attr_stm;
    int err;

    if (DEBUG_ATTR_STM_LEVEL_2)
        knd_log(".. import nested attr_stm list: \"%.*s\" REC: %.*s",
                name_size, name, 32, rec);

    err = knd_attr_stm_new(&attr_stm, mempool);
    if (err) {
        return make_gsl_err(err);
    }
    attr_stm->name = name;
    attr_stm->name_size = name_size;
    attr_stm->base_pred = parent_attr_stm->base_pred;

    attr_stm->next = parent_attr_stm->children;
    parent_attr_stm->children = attr_stm;
    parent_attr_stm->num_children++;

    struct LocalContext attr_stm_ctx = {
        .list_parent = attr_stm,
        .task = task
    };

    struct gslTaskSpec import_attr_stm_spec = {
        .is_list_item = true,
        .parse = import_attr_stm_list_item,
        .obj = &attr_stm_ctx
    };

    return gsl_parse_array(&import_attr_stm_spec, rec, total_size);
}

int knd_import_attr_stm(struct kndClassBasePred *self, const char *name, size_t name_size,
                        const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndAttrStm *attr_stm;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_ATTR_STM_LEVEL_2)
        knd_log(".. import attr var: \"%.*s\" REC: %.*s", name_size, name, 32, rec);

    err = knd_attr_stm_new(&attr_stm, mempool);
    if (err) return err;
    attr_stm->base_pred = self;
    attr_stm->name = name;
    attr_stm->name_size = name_size;

    struct LocalContext ctx = {
        .attr_stm = attr_stm,
        .task = task
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_stm_value,
          .obj = attr_stm
        },
        { .name = "_inst",
          .name_size = strlen("_inst"),
          .run = set_class_inst_ref,
          .obj = attr_stm
        },
        { .type = GSL_SET_STATE,
          .validate = import_nested_attr_stm,
          .obj = &ctx
        },
        { .validate = import_nested_attr_stm,
          .obj = &ctx
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = import_nested_attr_stm_list,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = import_nested_attr_stm_list,
          .obj = &ctx
        },
        /*{ .name = "_t",
          .name_size = strlen("_t"),
          .parse = parse_text,
          .obj = &ctx
        },
        { .name = "_cdata",
          .name_size = strlen("_cdata"),
          .obj = &cdata_spec
          }*/
        { .is_default = true,
          .run = confirm_attr_stm,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        KND_TASK_LOG("\"%.*s\" attr var import failed", name_size, name);
        return parser_err.code;
    }
    append_attr_stm(self, attr_stm);

    if (DEBUG_ATTR_STM_LEVEL_2)
        knd_log("++ attr var value: %.*s", attr_stm->val_size, attr_stm->val);

    return knd_OK;
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
    //attr_stm->list_count = self->num_list_elems;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_stm_list_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrStm *self = ctx->list_parent;
    struct kndAttrStm *attr_stm;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    int err;

    err = knd_attr_stm_new(&attr_stm, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_stm->base_pred = self->base_pred;
    attr_stm->is_list_item = true;
    attr_stm->parent = self;
    ctx->attr_stm = attr_stm;

    if (DEBUG_ATTR_STM_LEVEL_3) {
        knd_log("== importing a list item of %.*s: %.*s",
                self->name_size, self->name, 32, rec);
    }
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_stm_name,
          .obj = attr_stm
        },
        { .validate = import_nested_attr_stm,
          .obj = ctx
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = import_nested_attr_stm_list,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = import_nested_attr_stm_list,
          .obj = ctx
        },
        { .is_default = true,
          .run = confirm_attr_stm,
          .obj = ctx
        }        
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        return parser_err;
    }
    // append
    return append_attr_stm_list_item(self, attr_stm);
}

int knd_import_attr_stm_list(struct kndClassBasePred *bp, const char *name, size_t name_size,
                             const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndAttrStm *attr_stm;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_ATTR_STM_LEVEL_2) {
        knd_log("== import attr attr_stm list: \"%.*s\" REC: %.*s",
                name_size, name, 32, rec);
    }
    err = knd_attr_stm_new(&attr_stm, mempool);
    KND_TASK_ERR("failed to alloc an attr var");
    attr_stm->base_pred = bp;
    attr_stm->name = name;
    attr_stm->name_size = name_size;
    append_attr_stm(bp, attr_stm);

    struct LocalContext ctx = {
        .list_parent = attr_stm,
        //.attr_stm = attr_stm,
        .task = task
    };

    struct gslTaskSpec import_attr_stm_spec = {
        .is_list_item = true,
        .parse = import_attr_stm_list_item,
        .obj = &ctx
    };
    parser_err = gsl_parse_array(&import_attr_stm_spec, rec, total_size);
    if (parser_err.code) return parser_err.code;
    return knd_OK;
}

static gsl_err_t import_nested_attr_stm(void *obj, const char *name, size_t name_size,
                                        const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrStm *self = ctx->attr_stm;
    struct kndTask    *task = ctx->task;
    struct kndAttrStm *attr_stm;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    gsl_err_t parser_err;
    int err;
    
    err = knd_attr_stm_new(&attr_stm, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_stm->base_pred = self->base_pred;
    attr_stm->parent = self;
    attr_stm->name = name;
    attr_stm->name_size = name_size;

    ctx->attr_stm = attr_stm;

    if (DEBUG_ATTR_STM_LEVEL_2)
        knd_log(".. import nested attr var: \"%.*s\" (parent item:%.*s)",
                attr_stm->name_size, attr_stm->name, self->name_size, self->name);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_stm_value,
          .obj = attr_stm
        },
        { .name = "_inst",
          .name_size = strlen("_inst"),
          .run = set_class_inst_ref,
          .obj = attr_stm
        },
        /*{ .name = "_t",
          .name_size = strlen("_t"),
          .parse = parse_text,
          .obj = ctx
        },
        { .name = "_cdata",
          .name_size = strlen("_cdata"),
          .parse = parse_attr_stm_cdata,
          .obj = attr_stm
          }*/
        { .validate = import_nested_attr_stm,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = import_nested_attr_stm_list,
          .obj = ctx
        },
        { .is_default = true,
          .run = confirm_attr_stm,
          .obj = ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        KND_TASK_LOG("attr var import failed: %d", parser_err.code);
        return parser_err;
    }

    /* restore parent */
    ctx->attr_stm = self;

    if (DEBUG_ATTR_STM_LEVEL_3)
        knd_log("++ attr var: \"%.*s\" val:%.*s (parent item: %.*s)",
                attr_stm->name_size, attr_stm->name,
                attr_stm->val_size, attr_stm->val,
                self->name_size, self->name);

    attr_stm->next = self->children;
    self->children = attr_stm;
    self->num_children++;

    return make_gsl_err(gsl_OK);
}

static void append_attr_stm(struct kndClassBasePred *ci, struct kndAttrStm *attr_stm)
{
    struct kndAttrStm *curr_var;

    FOREACH (curr_var, ci->attr_stms) {
        if (curr_var->name_size != attr_stm->name_size) continue;
        if (!memcmp(curr_var->name, attr_stm->name, attr_stm->name_size)) {
            if (!curr_var->list_tail) {
                curr_var->list_tail = attr_stm;
                curr_var->list = attr_stm;
            }
            else {
                curr_var->list_tail->next = attr_stm;
                curr_var->list_tail = attr_stm;
            }
            curr_var->num_list_elems++;
            return;
        }
    }

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
