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

#define DEBUG_ATTR_VAR_GSL_LEVEL_1 0
#define DEBUG_ATTR_VAR_GSL_LEVEL_2 0
#define DEBUG_ATTR_VAR_GSL_LEVEL_3 0
#define DEBUG_ATTR_VAR_GSL_LEVEL_4 0
#define DEBUG_ATTR_VAR_GSL_LEVEL_5 0
#define DEBUG_ATTR_VAR_GSL_LEVEL_TMP 1

static int attr_var_list_export_GSL(struct kndAttrVar *parent_item, struct kndTask *task, size_t depth);

static int inner_var_export_GSL(struct kndAttrVar *var, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr = var->attr;
    struct kndClass *c;
    struct kndAttrVar *item;
    int err;

    if (DEBUG_ATTR_VAR_GSL_LEVEL_2)
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
        // TODO atomic
        c = var->attr->ref_class_entry->class;

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

    if (DEBUG_ATTR_VAR_GSL_LEVEL_2) {
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
        if (DEBUG_ATTR_VAR_GSL_LEVEL_2)
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

    if (DEBUG_ATTR_VAR_GSL_LEVEL_2) {
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

    if (DEBUG_ATTR_VAR_GSL_LEVEL_2)
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
    int err;

    if (task->ctx->depth >= task->ctx->max_depth) {
        if (DEBUG_ATTR_VAR_GSL_LEVEL_TMP)
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
        assert(var->text != NULL);
        OUT("{_t", strlen("{_t"));
        err = knd_text_export(var->text, KND_FORMAT_GSL, task);
        KND_TASK_ERR("GSL text export failed");
        OUT("}", strlen("}"));
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
