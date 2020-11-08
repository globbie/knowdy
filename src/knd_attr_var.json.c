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

#define DEBUG_ATTR_VAR_JSON_LEVEL_1 0
#define DEBUG_ATTR_VAR_JSON_LEVEL_2 0
#define DEBUG_ATTR_VAR_JSON_LEVEL_3 0
#define DEBUG_ATTR_VAR_JSON_LEVEL_4 0
#define DEBUG_ATTR_VAR_JSON_LEVEL_5 0
#define DEBUG_ATTR_VAR_JSON_LEVEL_TMP 1

static int attr_var_list_export_JSON(struct kndAttrVar *parent_var, struct kndTask *task, size_t depth);

static int inner_var_export_JSON(struct kndAttrVar *var, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr = var->attr;
    struct kndClass *c;
    struct kndAttrVar *item;
    int err;

    if (DEBUG_ATTR_VAR_JSON_LEVEL_TMP)
        knd_log(".. JSON export inner var \"%.*s\"  list item:%d",
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
        err = attr_var_list_export_JSON(var, task, depth + 1);
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
        err = knd_attr_var_export_JSON(item, task, depth + 1);
    }    
    return knd_OK;
}

static int ref_var_export_JSON(struct kndAttrVar *var, struct kndTask *task)
{
    struct kndClass *c;
    size_t curr_depth = 0;
    int err;

    assert(var->class != NULL);

    c = var->class;
    curr_depth = task->depth;
    task->depth++;
    err = knd_class_export_JSON(c, task);                                         RET_ERR();
    task->depth = curr_depth;

    return knd_OK;
}

static int proc_var_export_JSON(struct kndAttrVar *var,
                                 struct kndTask *task)
{
    assert(var->proc != NULL);
    int err = knd_proc_export_JSON(var->proc, task, false, 0);                   RET_ERR();
    return knd_OK;
}

static int attr_var_list_export_JSON(struct kndAttrVar *parent_var, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttrVar *var;
    bool in_list = false;
    size_t count = 0;
    int err;

    if (DEBUG_ATTR_VAR_JSON_LEVEL_2) {
        knd_log(".. export JSON list: %.*s\n\n",
                parent_var->name_size, parent_var->name);
    }

    OUT("\"", 1);
    OUT(parent_var->name, parent_var->name_size);
    OUT("\":[", strlen("\":["));

    FOREACH (var, parent_var->list) {
        if (in_list) {
            err = out->writec(out, ',');
            if (err) return err;
        }

        OUT("{", 1);
        switch (parent_var->attr->type) {
        case KND_ATTR_INNER:
            var->id_size = sprintf(var->id, "%lu", (unsigned long)count);
            count++;
            err = inner_var_export_JSON(var, task, depth + 1);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = ref_var_export_JSON(var, task);
            if (err) return err;
            break;
        case KND_ATTR_PROC_REF:
            if (var->proc) {
                err = proc_var_export_JSON(var, task);
                if (err) return err;
            }
            break;
        default:
            err = out->writec(out, '"');
            if (err) return err;
            err = out->write(out, var->val, var->val_size);
            if (err) return err;
            err = out->writec(out, '"');
            if (err) return err;
            break;
        }
        OUT("}", 1);
        in_list = true;
    }
    err = out->writec(out, ']');
    if (err) return err;

    return knd_OK;
}

int knd_attr_vars_export_JSON(struct kndAttrVar *vars, struct kndTask *task, bool is_concise, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttrVar *var;
    struct kndAttr *attr;
    struct kndClass *c;
    size_t curr_depth = 0;
    int err;

    for (var = vars; var; var = var->next) {
        if (!var->attr) continue;
        attr = var->attr;

        if (is_concise && !attr->concise_level) continue;

        //var->depth = vars->depth;
        //var->max_depth = vars->max_depth;

        err = out->writec(out, ',');
        if (err) return err;

        if (attr->is_a_set) {
            err = attr_var_list_export_JSON(var, task, depth);
            if (err) return err;
            continue;
        }

        err = out->writec(out, '"');
        if (err) return err;
        err = out->write(out, var->name, var->name_size);
        if (err) return err;
        err = out->write(out, "\":", strlen("\":"));
        if (err) return err;

        switch (var->attr->type) {
        case KND_ATTR_NUM:
            err = out->write(out, var->val, var->val_size);
            if (err) return err;
            break;
        case KND_ATTR_TEXT:
            err = knd_text_export(var->text, KND_FORMAT_JSON, task);
            KND_TASK_ERR("failed to export text JSON");
            break;
        case KND_ATTR_PROC_REF:
            if (var->proc) {
                err = proc_var_export_JSON(var, task);
                if (err) return err;
            } else {
                err = out->write(out, "\"", strlen("\""));
                if (err) return err;
                err = out->write(out, var->val, var->val_size);
                if (err) return err;
                err = out->write(out, "\"", strlen("\""));
                if (err) return err;
            }
            break;
        case KND_ATTR_INNER:
            if (!var->class) {
                err = inner_var_export_JSON(var, task, depth);
                if (err) return err;
            } else {
                c = var->class;
                curr_depth = task->depth;
                task->depth++;
                err = knd_class_export_JSON(c, task);  RET_ERR();
                task->depth = curr_depth;
            }
            break;
        default:
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
            err = out->write(out, var->val, var->val_size);
            if (err) return err;
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
        }
    }

    return knd_OK;
}

int knd_attr_var_export_JSON(struct kndAttrVar *var, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClass *c;
    int err;

    if (task->ctx->depth >= task->ctx->max_depth) return knd_OK;

    err = out->writec(out, '"');
    if (err) return err;
    err = out->write(out, var->name, var->name_size);
    if (err) return err;
    err = out->write(out, "\":", strlen("\":"));
    if (err) return err;
    
    switch (var->attr->type) {
    case KND_ATTR_NUM:
        err = out->write(out, var->val, var->val_size);
        if (err) return err;
        
        break;
    case KND_ATTR_PROC_REF:
        if (var->proc) {
            err = proc_var_export_JSON(var, task);
            if (err) return err;
        } else {
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
            err = out->write(out, var->val, var->val_size);
            if (err) return err;
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
        }
        break;
    case KND_ATTR_INNER:
        if (!var->class) {
            err = inner_var_export_JSON(var, task, depth);
            if (err) return err;
        } else {
            c = var->class;
            err = knd_class_export_JSON(c, task);
            if (err) return err;
        }
        break;
    case KND_ATTR_TEXT:
        assert(var->text != NULL);
        // OUT("\"_t\":{", strlen("\"_t\":"));
        err = knd_text_export(var->text, KND_FORMAT_JSON, task);
        KND_TASK_ERR("GSL text export failed");
        // OUT("}", strlen("}"));
        break;
    default:
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
            err = out->write(out, var->val, var->val_size);
            if (err) return err;
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
    }
    return knd_OK;
}
