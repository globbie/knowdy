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

#define DEBUG_ATTR_GSL_LEVEL_1 0
#define DEBUG_ATTR_GSL_LEVEL_2 0
#define DEBUG_ATTR_GSL_LEVEL_3 0
#define DEBUG_ATTR_GSL_LEVEL_4 0
#define DEBUG_ATTR_GSL_LEVEL_5 0
#define DEBUG_ATTR_GSL_LEVEL_TMP 1

struct LocalContext {
    struct kndClassVar *class_var;
    struct kndAttrVar  *list_parent;
    struct kndAttr     *attr;
    struct kndAttrVar  *attr_var;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

static int attr_var_list_export_GSL(struct kndAttrVar *parent_item,
                                    struct kndTask *task,
                                    size_t depth);
static gsl_err_t read_attr_var_list_item(void *obj,
                                         const char *rec,
                                         size_t *total_size);
static gsl_err_t read_nested_attr_var(void *obj,
                                      const char *name, size_t name_size,
                                      const char *rec, size_t *total_size);
static gsl_err_t set_attr_var_value(void *obj, const char *val, size_t val_size);
static gsl_err_t confirm_attr_var(void *obj,
                                  const char *unused_var(name),
                                  size_t unused_var(name_size));

static void append_attr_var(struct kndClassVar *ci,
                            struct kndAttrVar *attr_var)
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

static int inner_var_export_GSL(struct kndAttrVar *var, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr = var->attr;
    struct kndClass *c;
    struct kndAttrVar *item;
    int err;

    if (DEBUG_ATTR_GSL_LEVEL_TMP)
        knd_log(".. GSL export inner var \"%.*s\"  list item:%d",
                var->name_size, var->name, var->is_list_item);

    if (task->ctx->format_offset) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * task->ctx->format_offset);
        KND_TASK_ERR("GSL offset output failed");
    }

    if (var->implied_attr) {
        OUT(var->val, var->val_size);
    }

    if (var->is_list_item) {
        goto inner_children;
    }

    if (attr->is_a_set) {
        err = attr_var_list_export_GSL(var, task, depth + 1);
        KND_TASK_ERR("GSL attr var list export failed");
        return knd_OK;
    }
        
    /* export a class ref */
    if (var->class) {
        c = var->attr->ref_class;

        /* TODO: check assignment */
        if (var->implied_attr) {
            attr = var->implied_attr;
        }
        if (c->implied_attr)
            attr = c->implied_attr;
        c = var->class;

        err = knd_class_export_GSL(c->entry, task, false, depth + 1);
        KND_TASK_ERR("GSL class export failed");
    }

    if (!var->class) {
        /* terminal string value */
        if (var->val_size) {
            OUT(var->val, var->val_size);
        }
    }

 inner_children:
    for (item = var->children; item; item = item->next) {
        if (task->ctx->format_offset) {
            OUT("\n", 1);
            err = knd_print_offset(out, depth * task->ctx->format_offset);
            KND_TASK_ERR("GSL offset output failed");
        }
        err = knd_attr_var_export_GSL(item, task, depth + 1);
    }    
    return knd_OK;
}

extern int knd_export_inherited_attr_GSL(void *obj,
                                         const char *unused_var(elem_id),
                                         size_t unused_var(elem_id_size),
                                         size_t unused_var(count),
                                         void *elem)
{
    struct kndTask *task = obj;
    struct kndClass   *self = NULL; // TODO task->class;
    struct kndAttrRef *ref = elem;
    struct kndAttr *attr = ref->attr;
    struct kndAttrVar *attr_var = ref->attr_var;
    struct kndOutput *out = task->out;
    struct kndMemPool *mempool = task->mempool;
    size_t numval = 0;
    size_t depth = 1;
    int err;

    if (DEBUG_ATTR_GSL_LEVEL_2) {
        knd_log(".. class \"%.*s\" to export inherited attr \"%.*s\"..",
                self->name_size, self->name,
                attr->name_size, attr->name);
    }

    /* skip over immediate attrs */
    if (attr->parent_class == self) return knd_OK;

    if (attr_var && attr_var->class_var) {
        /* already exported by parent */
        if (attr_var->class_var->parent == self) return knd_OK;
    }

    /* NB: display only concise fields */
    if (!attr->concise_level) {
        return knd_OK;
    }

    if (attr->proc) {
        if (DEBUG_ATTR_GSL_LEVEL_2)
            knd_log("..computed attr: %.*s!", attr->name_size, attr->name);

        if (!attr_var) {
            err = knd_attr_var_new(mempool, &attr_var);                           RET_ERR();
            attr_var->attr = attr;
            attr_var->name = attr->name;
            attr_var->name_size = attr->name_size;
            ref->attr_var = attr_var;
        }

        switch (attr->type) {
        case KND_ATTR_NUM:
            numval = attr_var->numval;

            if (!attr_var->is_cached) {
                //err = knd_compute_class_attr_num_value(self, attr_var);
                //if (err) return err;
                numval = attr_var->numval;
                attr_var->numval = numval;
                attr_var->is_cached = true;
            }

            err = out->writec(out, ',');
            if (err) return err;
            err = out->writec(out, '"');
            if (err) return err;
            err = out->write(out, attr->name, attr->name_size);
            if (err) return err;
            err = out->writec(out, '"');
            if (err) return err;
            err = out->writec(out, ':');
            if (err) return err;
            
            err = out->writef(out, "%zu", numval);                                RET_ERR();
            break;
        default:
            break;
        }
        return knd_OK;
    }

    if (!attr_var) {
        // TODO
        return knd_OK;
        //err = knd_get_attr_var(self, attr->name, attr->name_size, &attr_var);
        //if (err) return knd_OK;
    }

    if (attr->is_a_set) {
        return attr_var_list_export_GSL(attr_var, task, depth);
    }

    err = out->writec(out, '{');                                          RET_ERR();
    err = out->write(out, attr_var->name, attr_var->name_size);           RET_ERR();

    switch (attr->type) {
    case KND_ATTR_NUM:
        err = out->writec(out, ' ');                            RET_ERR();
        err = out->write(out, attr_var->val, attr_var->val_size);             RET_ERR();
        break;
    case KND_ATTR_INNER:
        err = inner_var_export_GSL(attr_var, task, depth + 1);
        if (err) return err;
        break;
    case KND_ATTR_STR:
        err = out->writec(out, ' ');                            RET_ERR();
        err = out->write(out, attr_var->val, attr_var->val_size);             RET_ERR();
        break;
    default:
        break;
    }
    err = out->writec(out, '}');                                          RET_ERR();
    
    return knd_OK;
}

static int ref_item_export_GSL(struct kndAttrVar *item, struct kndTask *task, size_t depth)
{
    struct kndClass *c;
    size_t curr_depth = task->depth;
    int err;

    // TODO
    assert(item->class != NULL);
    c = item->class;

    if (DEBUG_ATTR_GSL_LEVEL_2) {
        knd_log(".. expand ref %.*s: depth:%zu max_depth:%zu",
                c->name_size, c->name, task->depth, task->max_depth);
    }

    err = knd_class_export_GSL(c->entry, task, false, depth);                            RET_ERR();
    task->depth = curr_depth;

    return knd_OK;
}

static int proc_item_export_GSL(struct kndAttrVar *item,
                                struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndProc *proc;
    int err;
    assert(item->proc != NULL);
    proc = item->proc;
    err = knd_proc_export(proc, KND_FORMAT_GSL, task, out);  RET_ERR();
    return knd_OK;
}

static int attr_var_list_export_GSL(struct kndAttrVar *var, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttrVar *item;
    size_t count = 0;
    int err;

    if (DEBUG_ATTR_GSL_LEVEL_TMP)
        knd_log(".. export GSL list attr \"%.*s\"", var->name_size, var->name);

    OUT("[", 1);
    OUT(var->name, var->name_size);

    for (item = var->list; item; item = item->next) {
        if (task->ctx->format_offset) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_offset(out, (depth + 1) * task->ctx->format_offset);       RET_ERR();
        }

        OUT("{", 1);
        switch (var->attr->type) {
        case KND_ATTR_INNER:
            item->id_size = sprintf(item->id, "%lu", (unsigned long)count);
            count++;
            err = inner_var_export_GSL(item, task, depth + 1);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = ref_item_export_GSL(item, task, depth + 1);
            if (err) return err;
            break;
        case KND_ATTR_PROC_REF:
            if (item->proc) {
                err = proc_item_export_GSL(item, task);
                if (err) return err;
            }
            break;
        case KND_ATTR_STR:
            OUT(item->name, item->name_size);
            break;
        default:
            OUT(item->val, item->val_size);
            break;
        }
        OUT("}", 1);
    }
    OUT("]", 1);

    return knd_OK;
}

int knd_attr_vars_export_GSL(struct kndAttrVar *vars, struct kndTask *task,
                             bool is_concise, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttrVar *var;
    struct kndAttr *attr;
    size_t curr_depth = task->ctx->depth;
    //size_t max_depth = task->ctx->max_depth;
    int err;

    for (var = vars; var; var = var->next) {
        if (!var->attr) continue;
        attr = var->attr;
        
        if (is_concise && !attr->concise_level) {
            //knd_log(".. concise level: %d", attr->concise_level);
            if (var->attr->type != KND_ATTR_INNER) 
                continue;
        }
        task->ctx->depth = curr_depth;

        if (task->ctx->format_offset) {
            OUT("\n", 1);
            err = knd_print_offset(out, depth * task->ctx->format_offset);
            KND_TASK_ERR("offset output failed");
        }

        if (attr->is_a_set) {
            err = attr_var_list_export_GSL(var, task, depth + 1);
            KND_TASK_ERR("attr var list GSL export failed");
            continue;
        }

        err = knd_attr_var_export_GSL(var, task, depth + 1);
        KND_TASK_ERR("attr var GSL export failed");
    }
    return knd_OK;
}

int knd_attr_var_export_GSL(struct kndAttrVar *var, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr = var->attr;
    struct kndClass *c;
    int err;

    if (task->ctx->depth >= task->ctx->max_depth) {
        if (DEBUG_ATTR_GSL_LEVEL_TMP)
            knd_log("NB: max depth reached: %zu", task->ctx->depth);
        return knd_OK;
    }

    if (var->is_list_item) {
        OUT("{", 1);
    } else {
        if (!attr->is_a_set) {
            OUT("{", 1);
            OUT(var->name, var->name_size);
            OUT(" ", 1);
        }
    }

    switch (var->attr->type) {
    case KND_ATTR_NUM:
        OUT(var->val, var->val_size);
        break;
    case KND_ATTR_ATTR_REF:
        if (var->ref_attr) {
	    err = knd_attr_export_GSL(var->ref_attr, task, depth + 1);           RET_ERR();
        } else {
            err = out->write(out, "_null", strlen("_null"));                      RET_ERR();
        }
        break;
    case KND_ATTR_PROC_REF:
        /*if (var->proc) {
            err = proc_var_export_GSL(var, task);
            KND_TASK_ERR("proc var GSL export failed");
        } else {
            OUT(var->val, var->val_size);
            }*/
        break;
    case KND_ATTR_INNER:
        err = inner_var_export_GSL(var, task, depth + 1);
        KND_TASK_ERR("GSL inner var output failed");
        break;
    case KND_ATTR_TEXT:
        err = knd_text_export(var->text, KND_FORMAT_GSL, task);
        KND_TASK_ERR("GSL text export failed");
        break;
    default:
        OUT(var->val, var->val_size);
        break;
    }

    if (var->is_list_item || !attr->is_a_set) {
        OUT("}", 1);
    } 
    return knd_OK;
}

extern int knd_attr_export_GSL(struct kndAttr *self, struct kndTask *task, size_t depth)
{
    char buf[KND_NAME_SIZE] = {0};
    size_t buf_size = 0;
    struct kndText *tr;
    struct kndOutput *out = task->out;
    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
    int err;

    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, type_name, type_name_size);                             RET_ERR();
    err = out->write(out, " ", 1);                                                RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();

    if (self->is_a_set) {
        err = out->write(out, " {t set}", strlen(" {t set}"));
        if (err) return err;
    }

    if (self->is_implied) {
        err = out->write(out, " {impl}", strlen(" {impl}"));
        if (err) return err;
    }

    if (self->is_indexed) {
        err = out->write(out, " {idx}", strlen(" {idx}"));
        if (err) return err;
    }

    if (self->concise_level) {
        buf_size = sprintf(buf, "%zu", self->concise_level);
        err = out->write(out, " {concise ", strlen(" {concise "));
        if (err) return err;
        err = out->write(out, buf, buf_size);
        if (err) return err;
        err = out->writec(out, '}');
        if (err) return err;
    }

    if (self->ref_classname_size) {
        err = out->write(out, " {c ", strlen(" {c "));
        if (err) return err;
        err = out->write(out, self->ref_classname, self->ref_classname_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }

    if (self->ref_procname_size) {
        err = out->write(out, " {p ", strlen(" {p "));
        if (err) return err;
        err = out->write(out, self->ref_procname, self->ref_procname_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }

    /* choose gloss */
    if (self->tr) {
        if (task->ctx->format_offset) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_offset(out, (depth + 1) * task->ctx->format_offset);  RET_ERR();
        }
        err = out->write(out,
                         "[_g", strlen("[_g"));
        if (err) return err;
    }

    for (tr = self->tr; tr; tr = tr->next) {
        err = out->write(out, "{", 1);
        if (err) return err;
        err = out->write(out, tr->locale,  tr->locale_size);
        if (err) return err;
        err = out->write(out, "{t ", 3);
        if (err) return err;
        err = out->write(out, tr->seq,  tr->seq_size);
        if (err) return err;
        err = out->write(out, "}}", 2);
        if (err) return err;
    }
    if (self->tr) {
        err = out->write(out, "]", 1);
        if (err) return err;
    }

    err = out->write(out, "}", 1);
    if (err) return err;

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

    if (DEBUG_ATTR_GSL_LEVEL_2)
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

static gsl_err_t read_class_var(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrVar *attr_var = ctx->attr_var;
    struct kndClassVar *class_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_ATTR_GSL_LEVEL_2)
        knd_log(".. reading a class var: \"%.*s\"", 32, rec);

    err = knd_class_var_new(mempool, &class_var);
    if (err) {
        KND_TASK_LOG("failed to alloc a class var");
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr_var->class_var = class_var;

    parser_err = knd_read_class_var(class_var, rec, total_size, task);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_nested_attr_var(void *obj,
                                      const char *name, size_t name_size,
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

    if (DEBUG_ATTR_GSL_LEVEL_2)
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

    if (DEBUG_ATTR_GSL_LEVEL_2)
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

    if (DEBUG_ATTR_GSL_LEVEL_2)
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

    if (DEBUG_ATTR_GSL_LEVEL_2)
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
    if (DEBUG_ATTR_GSL_LEVEL_2) {
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

    if (DEBUG_ATTR_GSL_LEVEL_2) {
        knd_log("== reading a list item of %.*s: %.*s",
                self->name_size, self->name, 32, rec);
    }

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

    if (DEBUG_ATTR_GSL_LEVEL_2)
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

int knd_read_attr_var(struct kndClassVar *self,
                      const char *name, size_t name_size,
                      const char *rec, size_t *total_size,
                      struct kndTask *task)
{
    struct kndAttrRef *attr_ref;
    struct kndAttrVar *attr_var;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_ATTR_GSL_LEVEL_2)
        knd_log("\n.. reading attr var \"%.*s\" of class: \"%.*s\"",
                name_size, name, self->entry->name_size, self->entry->name);

    err = knd_class_get_attr(self->entry->class, name, name_size, &attr_ref);
    KND_TASK_ERR("no attr \"%.*s\" in class \"%.*s\"",
                 name_size, name, self->entry->name_size, self->entry->name);

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
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = read_class_var,
          .obj = &ctx
        },
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
