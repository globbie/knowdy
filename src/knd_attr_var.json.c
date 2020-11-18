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
    size_t count = 0;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (DEBUG_ATTR_VAR_JSON_LEVEL_2)
        knd_log(".. JSON export inner var \"%.*s\"", var->name_size, var->name);

    if (var->implied_attr) {
        attr = var->implied_attr;
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth) * indent_size);
            RET_ERR();
        }
        OUT("\"", 1);
        OUT(attr->name, attr->name_size);
        OUT("\"", 1);
        OUT(":", 1);
        if (indent_size) {
            OUT(" ", 1);
        }
        switch (attr->type) {
        case KND_ATTR_REF:
            assert(var->class_entry != NULL);
            OUT("\"", 1);
            OUT(var->class_entry->name, var->class_entry->name_size);
            OUT("\"", 1);
            err = knd_class_acquire(var->class_entry, &c, task);
            KND_TASK_ERR("failed to acquire class %.*s",
                         var->class_entry->name_size, var->class_entry->name);
            if (c->tr) {
                err = knd_text_gloss_export_JSON(c->tr, task, depth);
                KND_TASK_ERR("failed to export gloss GSL");
            }
            count++;
            break;
        case KND_ATTR_STR:
            OUT("\"", 1);
            OUT(var->name, var->name_size);
            OUT("\"", 1);
            break;
        default:
            break;
        }
    }

    FOREACH (item, var->children) {
        if (DEBUG_ATTR_VAR_JSON_LEVEL_3)
            knd_log("* child %.*s => %.*s", item->name_size, item->name, item->val_size, item->val);
        
        err = knd_attr_var_export_JSON(item, task, depth);
        KND_TASK_ERR("failed to export JSON inner attr var");
        count++;
    }    
    return knd_OK;
}

static int ref_var_export_JSON(struct kndAttrVar *var, struct kndTask *task, size_t depth)
{
    struct kndClass *c;
    size_t curr_depth = 0;
    int err;

    assert(var->class != NULL);

    c = var->class;
    curr_depth = task->depth;
    task->depth++;
    err = knd_class_export_JSON(c, task, false, depth);
    RET_ERR();
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
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (DEBUG_ATTR_VAR_JSON_LEVEL_2)
        knd_log(".. export JSON list: %.*s\n\n", parent_var->name_size, parent_var->name);

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }

    OUT("\"", 1);
    OUT(parent_var->name, parent_var->name_size);
    OUT("\":", strlen("\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    OUT("[", 1);

    FOREACH (var, parent_var->list) {
        if (in_list) {
            err = out->writec(out, ',');
            if (err) return err;
        }

        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }

        OUT("{", 1);
        switch (parent_var->attr->type) {
        case KND_ATTR_INNER:
            var->id_size = sprintf(var->id, "%lu", (unsigned long)count);
            count++;
            err = inner_var_export_JSON(var, task, depth + 2);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = ref_var_export_JSON(var, task, depth + 1);
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

        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        OUT("}", 1);
        in_list = true;
    }

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }
    OUT("]", 1);

    return knd_OK;
}

int knd_attr_vars_export_JSON(struct kndAttrVar *vars, struct kndTask *task, bool is_concise, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttrVar *var;
    struct kndAttr *attr;
    struct kndClass *c;
    size_t curr_depth = 0;
    size_t indent_size = task->ctx->format_indent;
    int err;

    for (var = vars; var; var = var->next) {
        if (!var->attr) continue;
        attr = var->attr;

        if (is_concise && !attr->concise_level) continue;

        OUT(",", 1);
        if (attr->is_a_set) {
            err = attr_var_list_export_JSON(var, task, depth);
            if (err) return err;
            continue;
        }

        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, depth * indent_size);
            RET_ERR();
        }

        OUT("\"", 1);
        OUT(var->name, var->name_size);
        OUT("\":", strlen("\":"));
        if (indent_size) {
            OUT(" ", 1);
        }
        switch (var->attr->type) {
        case KND_ATTR_NUM:
        case KND_ATTR_FLOAT:
            OUT(var->val, var->val_size);
            break;
        case KND_ATTR_TEXT:
            err = knd_text_export(var->text, KND_FORMAT_JSON, task, depth);
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
                err = inner_var_export_JSON(var, task, depth + 2);
                if (err) return err;
            } else {
                c = var->class;
                curr_depth = task->depth;
                task->depth++;
                err = knd_class_export_JSON(c, task, false, depth + 2);
                RET_ERR();
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
    size_t indent_size = task->ctx->format_indent;
    int err;

    OUT(",", 1);
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }

    if (task->ctx->depth > task->ctx->max_depth) return knd_OK;

    OUT("\"", 1);
    OUT(var->name, var->name_size);
    OUT("\":", strlen("\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    
    switch (var->attr->type) {
    case KND_ATTR_NUM:
    case KND_ATTR_FLOAT:
        OUT(var->val, var->val_size);
        break;
    case KND_ATTR_PROC_REF:
        if (var->proc) {
            err = proc_var_export_JSON(var, task);
            if (err) return err;
        } else {
            OUT("\"", 1);
            OUT(var->val, var->val_size);
            OUT("\"", 1);
        }
        break;
    case KND_ATTR_INNER:
        if (var->class) {
            err = knd_class_export_JSON(var->class, task, false, depth + 1);
            if (err) return err;
            break;
        }
        err = inner_var_export_JSON(var, task, depth + 2);
        KND_TASK_ERR("failed to export inner var JSON");
        break;
    case KND_ATTR_TEXT:
        assert(var->text != NULL);
        // OUT("\"_t\":{", strlen("\"_t\":"));
        err = knd_text_export(var->text, KND_FORMAT_JSON, task, depth + 1);
        KND_TASK_ERR("GSL text export failed");
        // OUT("}", strlen("}"));
        break;
    default:
        OUT("\"", 1);
        OUT(var->val, var->val_size);
        OUT("\"", 1);
    }
    return knd_OK;
}
