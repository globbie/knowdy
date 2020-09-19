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
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#define DEBUG_ATTR_VAR_GSP_LEVEL_1 0
#define DEBUG_ATTR_VAR_GSP_LEVEL_2 0
#define DEBUG_ATTR_VAR_GSP_LEVEL_3 0
#define DEBUG_ATTR_VAR_GSP_LEVEL_4 0
#define DEBUG_ATTR_VAR_GSP_LEVEL_5 0
#define DEBUG_ATTR_VAR_GSP_LEVEL_TMP 1

struct LocalContext {
    struct kndClassVar *class_var;
    struct kndAttrVar  *list_parent;
    struct kndAttr     *attr;
    struct kndAttrVar  *attr_var;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

static gsl_err_t read_attr_var_list_item(void *obj, const char *rec, size_t *total_size);
static gsl_err_t read_nested_attr_var(void *obj, const char *name, size_t name_size,
                                      const char *rec, size_t *total_size);
static gsl_err_t set_attr_var_value(void *obj, const char *val, size_t val_size);
static gsl_err_t confirm_attr_var(void *obj, const char *unused_var(name), size_t unused_var(name_size));

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

static int ref_item_export_GSP(struct kndAttrVar *item, struct kndOutput *out)
{
    struct kndClass *c = item->class;
    int err;
    assert(c != NULL);
    err = out->write(out, c->entry->id, c->entry->id_size);                       RET_ERR();
    return knd_OK;
}

static int inner_item_export_GSP(struct kndAttrVar *parent_item, struct kndOutput *out)
{
    struct kndAttrVar *item;
    struct kndAttr *attr;
    int err;

    if (DEBUG_ATTR_VAR_GSP_LEVEL_2) {
        knd_log(".. GSP export inner item: %.*s (id:%.*s)",
                parent_item->name_size, parent_item->name,
                parent_item->id_size, parent_item->id);
    }

    /*if (parent_item->id_size) {
        err = out->write(out, parent_item->id, parent_item->id_size);
        if (err) return err;
    }*/

    if (parent_item->val_size) {
        err = out->write(out, parent_item->val, parent_item->val_size);
        if (err) return err;
    }
    
    for (item = parent_item->children; item; item = item->next) {
        err = out->writec(out, '{');                                              RET_ERR();
        attr = item->attr;

        err = out->write(out, attr->id, attr->id_size);                           RET_ERR();
        err = out->writec(out, ' ');                                              RET_ERR();

        switch (attr->type) {
        case KND_ATTR_REF:
            err = ref_item_export_GSP(item, out);                                 RET_ERR();
            break;
        case KND_ATTR_INNER:
            err = inner_item_export_GSP(item, out);                                RET_ERR();
            break;
        default:
            err = out->write(out, item->val, item->val_size);                     RET_ERR();
            break;
        }
        err = out->writec(out, '}');                                              RET_ERR();
    }

    return knd_OK;
}


static int proc_item_export_GSP(struct kndAttrVar *item,
                                struct kndTask *task,
                                struct kndOutput *out)
{
    struct kndProc *proc;
    int err;

    assert(item->proc != NULL);

    proc = item->proc;

    err = knd_proc_export(proc, KND_FORMAT_GSP, task, out);  RET_ERR();

    return knd_OK;
}

static int attr_var_list_export_GSP(struct kndAttrVar *parent_item,
                                    struct kndTask *task,
                                    struct kndOutput *out)
{
    struct kndAttrVar *item;
    struct kndClass *c;
    int err;

    if (DEBUG_ATTR_VAR_GSP_LEVEL_2)
        knd_log(".. export GSP list: %.*s\n\n",
                parent_item->name_size, parent_item->name);

    OUT("[", 1);
    OUT(parent_item->name, parent_item->name_size);
    FOREACH (item, parent_item->list) {
        OUT("{", 1);
        switch (item->attr->type) {
        case KND_ATTR_REF:
            c = item->class;
            OUT(c->entry->id, c->entry->id_size);
            break;
        case KND_ATTR_INNER:
            /* check implied field */
            c = item->class;
            assert(c != NULL);
            OUT(c->entry->id, c->entry->id_size);
            err = knd_attr_var_export_GSP(item, task, out);
            KND_TASK_ERR("failed to export inner attr var");
            break;
        default:
            OUT(item->name, item->name_size);
            err = knd_attr_var_export_GSP(item, task, out);
            KND_TASK_ERR("failed to export attr var");
        }
        OUT("}", 1);
    }
    OUT("]", 1);
    return knd_OK;
}

int knd_attr_vars_export_GSP(struct kndAttrVar *items, struct kndOutput *out, struct kndTask *task,
                             size_t unused_var(depth), bool is_concise)
{
    struct kndAttrVar *item;
    struct kndAttr *attr;
    int err;

    FOREACH (item, items) {
        if (!item->attr) continue;
        attr = item->attr;
        if (is_concise && !attr->concise_level) continue;

        if (attr->is_a_set) {
            err = attr_var_list_export_GSP(item, task, out);
            KND_TASK_ERR("failed to export attr var list");
            continue;
        }
        OUT("{", 1);
        OUT(attr->id, attr->id_size);
        OUT(" ", 1);
        err = knd_attr_var_export_GSP(item, task, out);
        KND_TASK_ERR("failed to export attr var");
        OUT("}", 1);
    }
    return knd_OK;
}

int knd_attr_var_export_GSP(struct kndAttrVar *item, struct kndTask *task, struct kndOutput *out)
{
    int err;
    
    switch (item->attr->type) {
    case KND_ATTR_NUM:
        err = out->write(out, item->val, item->val_size);                         RET_ERR();
        break;
    case KND_ATTR_REF:
        err = ref_item_export_GSP(item, out);
        KND_TASK_ERR("failed to export ref attr var");
        break;
    case KND_ATTR_PROC_REF:
        err = proc_item_export_GSP(item, task, out);                              RET_ERR();
        break;
    case KND_ATTR_INNER:
        err = inner_item_export_GSP(item, out);                                    RET_ERR();
        break;
    default:
        err = out->write(out, item->val, item->val_size);                     RET_ERR();
    }
    return knd_OK;
}

static gsl_err_t read_nested_attr_var_list(void *obj,
                                           const char *name, size_t name_size,
                                           const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndAttrVar *parent_attr_var = ctx->attr_var;
    struct kndAttrVar *attr_var;
    int err;

    if (DEBUG_ATTR_VAR_GSP_LEVEL_2)
        knd_log(".. import nested attr_var list: \"%.*s\" REC: %.*s",
                name_size, name, 32, rec);

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) {
            return make_gsl_err(err);
    }
    attr_var->name = name;
    attr_var->name_size = name_size;

    attr_var->next = parent_attr_var->children;
    parent_attr_var->children = attr_var;
    parent_attr_var->num_children++;

    struct LocalContext attr_var_ctx = {
        .list_parent = attr_var,
        .task = task
    };

    struct gslTaskSpec read_attr_var_spec = {
        .is_list_item = true,
        .parse = read_attr_var_list_item,
        .obj = &attr_var_ctx
    };

    return gsl_parse_array(&read_attr_var_spec, rec, total_size);
}

static gsl_err_t read_class_var(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrVar *attr_var = ctx->attr_var;
    struct kndClassVar *class_var;
    struct kndMemPool *mempool = task->mempool;
    //gsl_err_t parser_err;
    int err;

    if (DEBUG_ATTR_VAR_GSP_LEVEL_2)
        knd_log(".. reading a class var: \"%.*s\"", 32, rec);

    err = knd_class_var_new(mempool, &class_var);
    if (err) {
        KND_TASK_LOG("failed to alloc a class var");
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr_var->class_var = class_var;

    //parser_err = knd_read_class_var(class_var, rec, total_size, task);
    //if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_nested_attr_var(void *obj, const char *name, size_t name_size,
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

    if (DEBUG_ATTR_VAR_GSP_LEVEL_2)
        knd_log(".. read nested attr var: \"%.*s\" (parent item:%.*s)",
                attr_var->name_size, attr_var->name,
                self->name_size, self->name);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = attr_var
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = read_class_var,
          .obj = ctx
        },
        { .validate = read_nested_attr_var,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_nested_attr_var_list,
          .obj = ctx
        },
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

    if (DEBUG_ATTR_VAR_GSP_LEVEL_2)
        knd_log("++ attr var: \"%.*s\" val:%.*s (parent item: %.*s)",
                attr_var->name_size, attr_var->name,
                attr_var->val_size, attr_var->val,
                self->name_size, self->name);

    attr_var->next = self->children;
    self->children = attr_var;
    self->num_children++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_attr_var_name(void *obj, const char *name, size_t name_size)
{
    struct kndAttrVar *self = obj;

    if (DEBUG_ATTR_VAR_GSP_LEVEL_2)
        knd_log(".. set attr var name: %.*s is_list_item:%d val:%.*s",
                name_size, name, self->is_list_item,
                self->val_size, self->val);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_attr_var_value(void *obj, const char *val, size_t val_size)
{
    struct kndAttrVar *self = obj;

    if (DEBUG_ATTR_VAR_GSP_LEVEL_2)
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
    if (DEBUG_ATTR_VAR_GSP_LEVEL_2) {
        if (!attr_var->val_size)
            knd_log("NB: attr var value not set in %.*s (class: %.*s)",
                    attr_var->name_size, attr_var->name,
                    attr_var->class_var->entry->name_size,
                    attr_var->class_var->entry->name);
    }
    return make_gsl_err(gsl_OK);
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

static gsl_err_t read_attr_var_list_item(void *obj,
                                         const char *rec,
                                         size_t *total_size)
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

    if (DEBUG_ATTR_VAR_GSP_LEVEL_2)
        knd_log("== reading a list item of %.*s: %.*s",
                self->name_size, self->name, 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_name,
          .obj = attr_var
        },
        { .validate = read_nested_attr_var,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_nested_attr_var_list,
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

int knd_read_attr_var_list(struct kndClassVar *self,
                           const char *name, size_t name_size,
                           const char *rec, size_t *total_size,
                           struct kndTask *task)
{
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err, e;

    if (!self->entry) {
        knd_log("-- anonymous class var: %.*s?  REC:%.*s", 64, rec);
        struct kndOutput *log = task->log; 
        log->reset(log);
        e = log->write(log, "no baseclass name specified",
                     strlen("no baseclass name specified"));
        if (e) return e;
        task->http_code = HTTP_BAD_REQUEST;
        return knd_FAIL;
    } 

    if (DEBUG_ATTR_VAR_GSP_LEVEL_2)
        knd_log("== import attr attr_var list: \"%.*s\" REC: %.*s",
                name_size, name, 32, rec);

    err = knd_attr_var_new(mempool, &attr_var);                                   RET_ERR();
    attr_var->class_var = self;
    attr_var->name = name;
    attr_var->name_size = name_size;

    append_attr_var(self, attr_var);

    struct LocalContext ctx = {
        .list_parent = attr_var,
        //.attr_var = attr_var,
        .task = task
    };

    struct gslTaskSpec read_attr_var_spec = {
        .is_list_item = true,
        .parse = read_attr_var_list_item,
        .obj = &ctx
    };

    parser_err = gsl_parse_array(&read_attr_var_spec, rec, total_size);
    if (parser_err.code) return parser_err.code;

    return knd_OK;
}

int knd_read_attr_var(struct kndClassVar *self, const char *name, size_t name_size,
                      const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndAttrRef *attr_ref;
    struct kndAttrVar *attr_var;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_ATTR_VAR_GSP_LEVEL_TMP)
        knd_log(".. reading attr var \"%.*s\" of class: \"%.*s\"",
                name_size, name, self->entry->name_size, self->entry->name);

    assert(self->entry->class != NULL);

    err = knd_class_get_attr(self->entry->class, name, name_size, &attr_ref);
    KND_TASK_ERR("no attr \"%.*s\" in class \"%.*s\"", name_size, name, self->entry->name_size, self->entry->name);

    err = knd_attr_var_new(task->mempool, &attr_var);
    KND_TASK_ERR("failed to alloc an attr var");
    attr_var->class_var = self;
    attr_var->name = name;
    attr_var->name_size = name_size;
    attr_var->attr = attr_ref->attr;

    struct LocalContext ctx = {
        .attr_var = attr_var,
        .task = task
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = attr_var
        }/*,
        { .name = "class",
          .name_size = strlen("class"),
          .parse = read_class_var,
          .obj = &ctx
          }*/,
        { .validate = read_nested_attr_var,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_nested_attr_var_list,
          .obj = &ctx
        },
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = attr_var
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- attr var parsing failed: %d", parser_err.code);
        return parser_err.code;
    }

    append_attr_var(self, attr_var);
    return knd_OK;
}


