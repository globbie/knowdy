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
    struct kndAttrVar *item;
    struct kndClass *c;
    int err;

    if (DEBUG_ATTR_VAR_GSL_LEVEL_2)
        knd_log(".. GSL export inner var \"%.*s\" val:%.*s  list item:%d",
                var->name_size, var->name, var->val_size, var->val, var->is_list_item);

    if (var->implied_attr) {
        attr = var->implied_attr;

        if (DEBUG_ATTR_VAR_GSL_LEVEL_2)
            knd_log(">> implied inner var attr \"%.*s\"", attr->name_size, attr->name);

        switch (attr->type) {
        case KND_ATTR_REL:
            // fall through
        case KND_ATTR_REF:
            assert(var->class_entry != NULL);
            OUT(var->class_entry->name, var->class_entry->name_size);

            err = knd_class_acquire(var->class_entry, &c, task);
            KND_TASK_ERR("failed to acquire class %.*s",
                         var->class_entry->name_size, var->class_entry->name);
            if (c->tr) {
                err = knd_text_gloss_export_GSL(c->tr, true, task, depth);
                KND_TASK_ERR("failed to export gloss GSL");
            }
            break;
        case KND_ATTR_STR:
            OUT(var->name, var->name_size);
            break;
        default:
            break;
        }
    }

    FOREACH (item, var->children) {
        err = knd_attr_var_export_GSL(item, task, depth);
        KND_TASK_ERR("failed to export inner var GSL");
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

    if (DEBUG_ATTR_VAR_GSL_LEVEL_2)
        knd_log(".. class \"%.*s\" to export inherited attr \"%.*s\"..",
                self->name_size, self->name, attr->name_size, attr->name);

    /* skip over immediate attrs */
    if (attr->parent == self) return knd_OK;

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

static int ref_var_export_GSL(struct kndAttrVar *var, struct kndTask *task, size_t unused_var(depth))
{
    struct kndOutput *out = task->out;
    assert(var->class_entry != NULL);
    OUT(var->class_entry->name, var->class_entry->name_size);
    return knd_OK;
}

static int attr_var_list_export_GSL(struct kndAttrVar *var, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr = var->attr;
    struct kndAttrVar *item;
    size_t count = 0;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (DEBUG_ATTR_VAR_GSL_LEVEL_2)
        knd_log(".. export GSL list attr \"%.*s\"", var->name_size, var->name);

    OUT("[", 1);
    OUT(var->name, var->name_size);

    FOREACH (item, var->list) {
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        OUT("{", 1);
        switch (attr->type) {
        case KND_ATTR_INNER:
            item->id_size = sprintf(item->id, "%lu", (unsigned long)count);
            count++;
            err = inner_var_export_GSL(item, task, depth + 2);
            if (err) return err;
            break;
        case KND_ATTR_REL:
            // fall through
        case KND_ATTR_REF:
            err = ref_var_export_GSL(item, task, depth + 2);
            if (err) return err;
            break;
        case KND_ATTR_PROC_REF:
            /*if (item->proc) {
                err = proc_item_export_GSL(item, task);
                if (err) return err;
                }*/
            OUT(item->val, item->val_size);
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
    size_t indent_size = task->ctx->format_indent;
    size_t count = 0;
    int err;

    FOREACH (var, vars) {
        assert(var->attr != NULL);
        attr = var->attr;
        if (DEBUG_ATTR_VAR_GSL_LEVEL_3)
            knd_log(">> attr var GSL export: %.*s", attr->name_size, attr->name);

        if (is_concise && !attr->concise_level) {
            //knd_log(".. concise level: %d", attr->concise_level);
            if (var->attr->type != KND_ATTR_INNER) 
                continue;
        }
        task->ctx->depth = curr_depth;

        if (indent_size && count) {
            OUT("\n", 1);
            err = knd_print_offset(out, depth * indent_size);
            KND_TASK_ERR("offset output failed");
        }
        count++;

        if (attr->is_a_set) {
            err = attr_var_list_export_GSL(var, task, depth);
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
    size_t indent_size = task->ctx->format_indent;
    int err;

    assert(attr != NULL);

    if (task->ctx->depth >= task->ctx->max_depth) {
        if (DEBUG_ATTR_VAR_GSL_LEVEL_TMP)
            knd_log("NB: max depth reached: %zu", task->ctx->depth);
        return knd_OK;
    }
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        KND_TASK_ERR("GSL offset output failed");
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
    case KND_ATTR_REL:
        break;
    case KND_ATTR_REF:
        assert(var->class_entry != NULL);
        OUT(var->class_entry->name, var->class_entry->name_size);

        err = knd_class_acquire(var->class_entry, &c, task);
        KND_TASK_ERR("failed to acquire class %.*s",
                     var->class_entry->name_size, var->class_entry->name);
        if (c->tr) {
            err = knd_text_gloss_export_GSL(c->tr, true, task, depth + 1);
            KND_TASK_ERR("failed to export gloss GSL");
        }
        
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
            } else { */
        OUT(var->val, var->val_size);
        break;
    case KND_ATTR_INNER:
        err = inner_var_export_GSL(var, task, depth + 1);
        KND_TASK_ERR("GSL inner var output failed");
        break;
    case KND_ATTR_TEXT:
        assert(var->text != NULL);
        err = knd_text_export(var->text, KND_FORMAT_GSL, task, depth + 1);
        KND_TASK_ERR("GSL text export failed");
        break;
    case KND_ATTR_BOOL:
        OUT("t", 1);
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
