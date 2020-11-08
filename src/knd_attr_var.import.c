#include "knd_attr.h"

#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#include <assert.h>
#include <string.h>

#include "knd_task.h"
#include "knd_class.h"
#include "knd_proc.h"
#include "knd_text.h"
#include "knd_output.h"

#define DEBUG_ATTR_VAR_LEVEL_1 0
#define DEBUG_ATTR_VAR_LEVEL_2 0
#define DEBUG_ATTR_VAR_LEVEL_3 0
#define DEBUG_ATTR_VAR_LEVEL_4 0
#define DEBUG_ATTR_VAR_LEVEL_5 0
#define DEBUG_ATTR_VAR_LEVEL_TMP 1

struct LocalContext {
    struct kndClassVar *class_var;
    struct kndAttrVar  *list_parent;
    struct kndAttr     *attr;
    struct kndAttrVar  *attr_var;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

static gsl_err_t import_attr_var_list_item(void *obj, const char *rec, size_t *total_size);

static gsl_err_t import_nested_attr_var(void *obj, const char *name, size_t name_size,
                                        const char *rec, size_t *total_size);
static void append_attr_var(struct kndClassVar *ci, struct kndAttrVar *attr_var);

static gsl_err_t set_attr_var_name(void *obj, const char *name, size_t name_size)
{
    struct kndAttrVar *self = obj;

    if (DEBUG_ATTR_VAR_LEVEL_2)
        knd_log(".. set attr var name: %.*s is_list_item:%d val:%.*s",
                name_size, name, self->is_list_item,
                self->val_size, self->val);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;

    return make_gsl_err(gsl_OK);
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

    parser_err = knd_text_import(text, rec, total_size, task);
    if (parser_err.code) {
        KND_TASK_LOG("text import failed");
        return parser_err;
    }
    ctx->attr_var->text = text;
    text->attr_var = ctx->attr_var;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_attr_var_value(void *obj, const char *val, size_t val_size)
{
    struct kndAttrVar *self = obj;

    if (DEBUG_ATTR_VAR_LEVEL_2)
        knd_log(".. set attr var value: \"%.*s\" => \"%.*s\"",
                self->name_size, self->name, val_size, val);

    if (!val_size) return make_gsl_err(gsl_FORMAT);

    self->val = val;
    self->val_size = val_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr_var(void *obj,
                                  const char *unused_var(name),
                                  size_t unused_var(name_size))
{
    struct kndAttrVar *attr_var = obj;

    // TODO empty values?
    if (DEBUG_ATTR_VAR_LEVEL_TMP) {
        if (!attr_var->val_size)
            knd_log("NB: attr var value not set in %.*s (class: %.*s)",
                    attr_var->name_size, attr_var->name,
                    attr_var->class_var->entry->name_size,
                    attr_var->class_var->entry->name);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_nested_attr_var_list(void *obj,
                                             const char *name, size_t name_size,
                                             const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndAttrVar *parent_attr_var = ctx->attr_var;
    struct kndAttrVar *attr_var;
    int err;

    if (DEBUG_ATTR_VAR_LEVEL_2)
        knd_log(".. import nested attr_var list: \"%.*s\" REC: %.*s", name_size, name, 32, rec);

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) {
        return make_gsl_err(err);
    }
    attr_var->name = name;
    attr_var->name_size = name_size;
    attr_var->class_var = parent_attr_var->class_var;

    attr_var->next = parent_attr_var->children;
    parent_attr_var->children = attr_var;
    parent_attr_var->num_children++;

    struct LocalContext attr_var_ctx = {
        .list_parent = attr_var,
        .task = task
    };

    struct gslTaskSpec import_attr_var_spec = {
        .is_list_item = true,
        .parse = import_attr_var_list_item,
        .obj = &attr_var_ctx
    };

    return gsl_parse_array(&import_attr_var_spec, rec, total_size);
}

int knd_import_attr_var(struct kndClassVar *self, const char *name, size_t name_size,
                        const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_ATTR_VAR_LEVEL_2)
        knd_log(".. import attr var: \"%.*s\" REC: %.*s", name_size, name, 32, rec);

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return err;
    attr_var->class_var = self;
    attr_var->name = name;
    attr_var->name_size = name_size;

    struct LocalContext ctx = {
        .attr_var = attr_var,
        .task = task
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = attr_var
        },
        { .type = GSL_SET_STATE,
          .validate = import_nested_attr_var,
          .obj = &ctx
        },
        { .validate = import_nested_attr_var,
          .obj = &ctx
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = import_nested_attr_var_list,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = import_nested_attr_var_list,
          .obj = &ctx
        },
        { .name = "_t",
          .name_size = strlen("_t"),
          .parse = parse_text,
          .obj = &ctx
        },
        /*{ .name = "_cdata",
          .name_size = strlen("_cdata"),
          .obj = &cdata_spec
          }*/
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = attr_var
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        KND_TASK_LOG("\"%.*s\" attr var import failed", name_size, name);
        return parser_err.code;
    }

    append_attr_var(self, attr_var);

    if (DEBUG_ATTR_VAR_LEVEL_2)
        knd_log("++ attr var value: %.*s", attr_var->val_size, attr_var->val);

    return knd_OK;
}

static gsl_err_t append_attr_var_list_item(void *accu,
                                           void *obj)
{
    struct kndAttrVar *self = accu;
    struct kndAttrVar *attr_var = obj;

    if (!self->list_tail) {
        self->list_tail = attr_var;
        self->list = attr_var;
    }
    else {
        self->list_tail->next = attr_var;
        self->list_tail = attr_var;
    }
    self->num_list_elems++;
    //attr_var->list_count = self->num_list_elems;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_var_list_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrVar *self = ctx->list_parent;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    int err;

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = self->class_var;
    attr_var->is_list_item = true;
    attr_var->parent = self;
    ctx->attr_var = attr_var;

    if (DEBUG_ATTR_VAR_LEVEL_2)
        knd_log("== importing a list item of %.*s: %.*s", self->name_size, self->name, 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_name,
          .obj = attr_var
        },
        { .validate = import_nested_attr_var,
          .obj = ctx
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = import_nested_attr_var_list,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = import_nested_attr_var_list,
          .obj = ctx
        },
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = attr_var
        }        
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        return parser_err;
    }
    // append
    return append_attr_var_list_item(self, attr_var);
}

int knd_import_attr_var_list(struct kndClassVar *self, const char *name, size_t name_size,
                             const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err, e;

    if (!self->entry) {
        knd_log("-- anonymous class var: %.*s?  REC:%.*s", 64, rec);
        struct kndOutput *log = task->log; 
        log->reset(log);
        e = log->write(log, "no baseclass name specified", strlen("no baseclass name specified"));
        if (e) return e;
        task->http_code = HTTP_BAD_REQUEST;
        return knd_FAIL;
    }

    if (DEBUG_ATTR_VAR_LEVEL_2)
        knd_log("== import attr attr_var list: \"%.*s\" REC: %.*s", name_size, name, 32, rec);

    err = knd_attr_var_new(mempool, &attr_var);
    KND_TASK_ERR("failed to alloc an attr var");
    attr_var->class_var = self;
    attr_var->name = name;
    attr_var->name_size = name_size;
    append_attr_var(self, attr_var);

    struct LocalContext ctx = {
        .list_parent = attr_var,
        //.attr_var = attr_var,
        .task = task
    };

    struct gslTaskSpec import_attr_var_spec = {
        .is_list_item = true,
        .parse = import_attr_var_list_item,
        .obj = &ctx
    };
    parser_err = gsl_parse_array(&import_attr_var_spec, rec, total_size);
    if (parser_err.code) return parser_err.code;
    return knd_OK;
}

static gsl_err_t import_nested_attr_var(void *obj, const char *name, size_t name_size,
                                        const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrVar *self = ctx->attr_var;
    struct kndTask    *task = ctx->task;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;
    
    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = self->class_var;
    attr_var->parent = self;

    attr_var->name = name;
    attr_var->name_size = name_size;

    ctx->attr_var = attr_var;

    if (DEBUG_ATTR_VAR_LEVEL_2)
        knd_log(".. import nested attr var: \"%.*s\" (parent item:%.*s)",
                attr_var->name_size, attr_var->name,
                self->name_size, self->name);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = attr_var
        },
        { .validate = import_nested_attr_var,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = import_nested_attr_var_list,
          .obj = ctx
        },
        { .name = "_t",
          .name_size = strlen("_t"),
          .parse = parse_text,
          .obj = ctx
        },
        /*{ .name = "_cdata",
          .name_size = strlen("_cdata"),
          .parse = parse_attr_var_cdata,
          .obj = attr_var
          }*/
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = attr_var
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        KND_TASK_LOG("attr var import failed: %d", parser_err.code);
        return parser_err;
    }

    /* restore parent */
    ctx->attr_var = self;

    if (DEBUG_ATTR_VAR_LEVEL_2)
        knd_log("++ attr var: \"%.*s\" val:%.*s (parent item: %.*s)",
                attr_var->name_size, attr_var->name,
                attr_var->val_size, attr_var->val,
                self->name_size, self->name);

    attr_var->next = self->children;
    self->children = attr_var;
    self->num_children++;

    return make_gsl_err(gsl_OK);
}

static void append_attr_var(struct kndClassVar *ci, struct kndAttrVar *attr_var)
{
    struct kndAttrVar *curr_var;

    for (curr_var = ci->attrs; curr_var; curr_var = curr_var->next) {
        if (curr_var->name_size != attr_var->name_size) continue;
        if (!memcmp(curr_var->name, attr_var->name, attr_var->name_size)) {
            if (!curr_var->list_tail) {
                curr_var->list_tail = attr_var;
                curr_var->list = attr_var;
            }
            else {
                curr_var->list_tail->next = attr_var;
                curr_var->list_tail = attr_var;
            }
            curr_var->num_list_elems++;
            return;
        }
    }

    if (!ci->tail) {
        ci->tail  = attr_var;
        ci->attrs = attr_var;
    }
    else {
        ci->tail->next = attr_var;
        ci->tail = attr_var;
    }
    ci->num_attrs++;
}
