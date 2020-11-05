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

static int attr_var_list_export_JSON(struct kndAttrVar *parent_var,
                                     struct kndTask *task);

static int inner_var_export_JSON(struct kndAttrVar *parent_var, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndAttrVar *var;
    struct kndAttr *attr;
    struct kndClass *c;
    bool in_list = false;
    size_t curr_depth = 0;
    int err;

    c = parent_var->attr->parent_class;

    if (DEBUG_ATTR_VAR_JSON_LEVEL_2) {
        knd_log(".. JSON export inner var: %.*s",
                parent_var->name_size, parent_var->name);
    }

    err = out->writec(out, '{');
    if (err) return err;

    if (parent_var->id_size) {
        err = out->write(out, "\"_id\":", strlen("\"_id\":"));
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
        err = out->write(out, parent_var->id, parent_var->id_size);
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
        in_list = true;

        // TODO atomic
        c = parent_var->attr->ref_class_entry->class;

        /*if (c->num_computed_attrs) {
            if (DEBUG_ATTR_VAR_JSON_LEVEL_2)
                knd_log(".. present computed attrs in %.*s (val:%.*s)",
                        parent_var->name_size, parent_var->name,
                        parent_var->val_size, parent_var->val);

            err = knd_present_computed_inner_attrs(parent_var, task);
            if (err) return err;
            }*/
    }

    /* export a class ref */
    if (parent_var->class) {
        attr = parent_var->attr;
        // TODO atomic
        c = parent_var->attr->ref_class_entry->class;

        /* TODO: check assignment */
        if (parent_var->implied_attr) {
            attr = parent_var->implied_attr;
        }

        if (c->implied_attr)
            attr = c->implied_attr;

        c = parent_var->class;

        if (in_list) {
            err = out->writec(out, ',');
            if (err) return err;
        }

        err = out->writec(out, '"');
        if (err) return err;
        err = out->write(out, attr->name, attr->name_size);
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
        err = out->writec(out, ':');
        if (err) return err;

        curr_depth = task->ctx->depth;
        task->ctx->depth++;
        err = knd_class_export_JSON(c, task);   RET_ERR();
        task->ctx->depth = curr_depth;
        in_list = true;
    }

    if (!parent_var->class) {
        /* terminal string value */
        if (parent_var->val_size) {
            // TODO atomic
            c = parent_var->attr->ref_class_entry->class;
            attr = parent_var->attr;

            if (c->implied_attr) {
                attr = c->implied_attr;
                err = out->writec(out, '"');
                if (err) return err;
                err = out->write(out, attr->name, attr->name_size);
                if (err) return err;
                err = out->writec(out, '"');
                if (err) return err;
                err = out->writec(out, ':');
                if (err) return err;
            } else {
                err = out->write(out, "\"_val\":", strlen("\"_val\":"));
                if (err) return err;
            }

            /* string or numeric value? */
            switch (attr->type) {
            case KND_ATTR_NUM:
                err = out->write(out, parent_var->val, parent_var->val_size);
                if (err) return err;
                break;
            default:
                err = out->writec(out, '"');
                if (err) return err;
                err = out->write(out, parent_var->val, parent_var->val_size);
                if (err) return err;
                err = out->writec(out, '"');
                if (err) return err;
            }
            in_list = true;
        }
    }

    for (var = parent_var->children; var; var = var->next) {
        if (in_list) {
            err = out->writec(out, ',');
            if (err) return err;
        }

        err = out->writec(out, '"');
        if (err) return err;
        err = out->write(out, var->name, var->name_size);
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
        err = out->writec(out, ':');
        if (err) return err;

        switch (var->attr->type) {
        case KND_ATTR_REF:
            knd_log("ref:%.*s  %.*s",
                    var->name_size, var->name,
                    var->val_size, var->val);
            assert(var->class != NULL);
            c = var->class;
            curr_depth = task->depth;
            task->depth++;
            err = knd_class_export_JSON(c, task);
            KND_TASK_ERR("failed to export class JSON");
            task->depth = curr_depth;
            break;
        case KND_ATTR_TEXT:
            err = knd_text_export(var->text, KND_FORMAT_JSON, task);
            KND_TASK_ERR("failed to export text JSON");
            break;
        case KND_ATTR_INNER:
            err = inner_var_export_JSON(var, task);
            KND_TASK_ERR("failed to export inner var JSON");
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

        in_list = true;
    }

    err = out->writec(out, '}');
    if (err) return err;

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

static int attr_var_list_export_JSON(struct kndAttrVar *parent_var, struct kndTask *task)
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

    err = out->writec(out, '"');
    if (err) return err;
    err = out->write(out, parent_var->name, parent_var->name_size);
    if (err) return err;
    err = out->write(out, "\":[", strlen("\":["));
    if (err) return err;


    for (var = parent_var->list; var; var = var->next) {
        /* TODO */
        if (!var->attr) {
            knd_log("-- no attr: %.*s (%p)",
                    var->name_size, var->name, var);
            continue;
        }

        if (in_list) {
            err = out->writec(out, ',');
            if (err) return err;
        }

        switch (parent_var->attr->type) {
        case KND_ATTR_INNER:
            var->id_size = sprintf(var->id, "%lu",
                                    (unsigned long)count);
            count++;
            err = inner_var_export_JSON(var, task);
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
        in_list = true;
    }
    err = out->writec(out, ']');
    if (err) return err;

    return knd_OK;
}

int knd_attr_vars_export_JSON(struct kndAttrVar *vars, struct kndTask *task, bool is_concise, size_t unused_var(depth))
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
            err = attr_var_list_export_JSON(var, task);
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
                err = inner_var_export_JSON(var, task);
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

int knd_attr_var_export_JSON(struct kndAttrVar *var, struct kndTask *task, size_t unused_var(depth))
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
            err = inner_var_export_JSON(var, task);
            if (err) return err;
        } else {
            c = var->class;
            err = knd_class_export_JSON(c, task);
            if (err) return err;
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
    return knd_OK;
}
