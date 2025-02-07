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
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#define DEBUG_ATTR_STM_JSON_LEVEL_1 0
#define DEBUG_ATTR_STM_JSON_LEVEL_2 0
#define DEBUG_ATTR_STM_JSON_LEVEL_3 0
#define DEBUG_ATTR_STM_JSON_LEVEL_4 0
#define DEBUG_ATTR_STM_JSON_LEVEL_5 0
#define DEBUG_ATTR_STM_JSON_LEVEL_TMP 1

static int attr_stm_list_export_JSON(struct kndAttrStm *parent_var, struct kndTask *task, size_t depth);

static int inner_stm_export_JSON(struct kndAttrStm *stm, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr = stm->attr;
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndClassRefAttrStm *cref;
    struct kndAttrStm *item;
    size_t count = 0;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (DEBUG_ATTR_STM_JSON_LEVEL_2)
        knd_log(".. JSON export inner stm \"%.*s\"", stm->name_size, stm->name);

    if (stm->implied_attr) {
        attr = stm->implied_attr;
        count++;
        OUT("\"", 1);
        OUT(attr->name, attr->name_size);
        OUT("\"", 1);
        OUT(":", 1);
        if (indent_size) {
            OUT(" ", 1);
        }
        switch (attr->type) {
        case KND_ATTR_CLASS_REF:
            assert(stm->subtype != NULL);
            cref = stm->subtype;
            entry = cref->cls_entry;

            OUT("\"", 1);
            OUT(entry->name, entry->name_size);
            OUT("\"", 1);
            err = knd_class_acquire(entry, &c, task);
            KND_TASK_ERR("failed to acquire class %.*s",
                         entry->name_size, entry->name);
            if (c->tr) {
                err = knd_text_gloss_export_JSON(c->tr, task, depth);
                KND_TASK_ERR("failed to export gloss GSL");
            }
            break;
        case KND_ATTR_STR:
            OUT("\"", 1);
            OUT(stm->name, stm->name_size);
            OUT("\"", 1);
            break;
        default:
            break;
        }
    }

    FOREACH (item, stm->children) {
        if (DEBUG_ATTR_STM_JSON_LEVEL_2)
            knd_log("* inner stm child %.*s => %.*s",
                    item->name_size, item->name, item->val_size, item->val);
        if (count) {
            OUT(",", 1);
            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth) * indent_size);
                RET_ERR();
            }
        }

        if (item->attr->is_a_set) {
            err = attr_stm_list_export_JSON(item, task, depth);
            KND_TASK_ERR("failed to export inner stm list JSON");
            continue;
        }

        err = knd_attr_stm_export_JSON(item, task, depth + 1);
        KND_TASK_ERR("failed to export JSON inner attr stm");
        count++;
    }    
    return knd_OK;
}

static int ref_stm_export_JSON(struct kndAttrStm *stm, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    size_t indent_size = task->ctx->format_indent;
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndClassRefAttrStm *cref;
    int err;

    assert(stm->subtype != NULL);
    cref = stm->subtype;
    entry = cref->cls_entry;

    OUT("\"class\":", strlen("\"class\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    OUT("\"", 1);
    OUT(entry->name, entry->name_size);
    OUT("\"", 1);

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);
    if (c->tr) {
        err = knd_text_gloss_export_JSON(c->tr, task, depth);
        KND_TASK_ERR("failed to export gloss GSL");
    }
    return knd_OK;
}

#if 0
static int proc_stm_export_JSON(struct kndAttrStm *stm, struct kndTask *task)
{
    assert(stm->proc_entry != NULL);
    // int err = knd_proc_export_JSON(stm->proc, task, false, 0);                   RET_ERR();
    return knd_OK;
}
#endif

static int attr_stm_list_export_JSON(struct kndAttrStm *parent_stm, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttrStm *stm;
    bool in_list = false;
    size_t count = 0;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (DEBUG_ATTR_STM_JSON_LEVEL_2)
        knd_log(".. export JSON list: %.*s\n\n", parent_stm->name_size, parent_stm->name);

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }
    OUT("\"", 1);
    OUT(parent_stm->name, parent_stm->name_size);
    OUT("\":", strlen("\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    OUT("[", 1);

    FOREACH (stm, parent_stm->list) {
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
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 2) * indent_size);
            RET_ERR();
        }
        switch (parent_stm->attr->type) {
        case KND_ATTR_INNER:
            stm->id_size = sprintf(stm->id, "%lu", (unsigned long)count);
            count++;
            err = inner_stm_export_JSON(stm, task, depth + 2);
            if (err) return err;
            break;
        case KND_ATTR_CLASS_REF:
            err = ref_stm_export_JSON(stm, task, depth + 1);
            if (err) return err;
            break;
        case KND_ATTR_STR:
            OUT("\"val\":", strlen("\"val\":"));
            if (indent_size) {
                OUT(" ", 1);
            }
            OUT("\"", 1);
            OUT(stm->name, stm->name_size);
            OUT("\"", 1);
            break;
        default:
            OUT("\"", 1);
            if (stm->val_size) {
                OUT(stm->val, stm->val_size);
            }
            OUT("\"", 1);
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

int knd_attr_stms_export_JSON(struct kndAttrStm *stms, struct kndTask *task,
                              bool is_concise, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttrStm *stm;
    struct kndAttr *attr;
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndClassRefAttrStm *cref;
    size_t indent_size = task->ctx->format_indent;
    int err;

    FOREACH (stm, stms) {
        assert(stm->attr != NULL);
        attr = stm->attr;
        if (is_concise && !attr->concise_level) continue;

        OUT(",", 1);
        if (attr->is_a_set) {
            err = attr_stm_list_export_JSON(stm, task, depth);
            if (err) return err;
            continue;
        }
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, depth * indent_size);
            RET_ERR();
        }
        OUT("\"", 1);
        OUT(stm->name, stm->name_size);
        OUT("\":", strlen("\":"));
        if (indent_size) {
            OUT(" ", 1);
        }
        switch (stm->attr->type) {
        case KND_ATTR_UINT:
            // fall through
        case KND_ATTR_UREAL:
            OUT(stm->val, stm->val_size);
            break;
        case KND_ATTR_CLASS_REF:
            assert(stm->subtype != NULL);
            cref = stm->subtype;
            entry = cref->cls_entry;

            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 1) * indent_size);
                RET_ERR();
            }
            OUT("{", 1);
            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 2) * indent_size);
                RET_ERR();
            }
            OUT("\"class\":", strlen("\"class\":"));
            if (indent_size) {
                OUT(" ", 1);
            }
            OUT("\"", 1);
            OUT(entry->name, entry->name_size);
            OUT("\"", 1);
            err = knd_class_acquire(entry, &c, task);
            KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);
            if (c->tr) {
                err = knd_text_gloss_export_JSON(c->tr, task, depth + 2);
                KND_TASK_ERR("failed to export gloss JSON");
            }
            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 1) * indent_size);
                RET_ERR();
            }
            OUT("}", 1);
            break;
        case KND_ATTR_TEXT:
            err = knd_text_export(stm->subtype, KND_FORMAT_JSON, task, depth);
            KND_TASK_ERR("failed to export text JSON");
            break;
        case KND_ATTR_INNER:
            OUT("{", 1);
            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 2) * indent_size);
                RET_ERR();
            }
            /*if (!stm->class) {
                err = inner_stm_export_JSON(stm, task, depth + 2);
                if (err) return err;
            } else {
                c = stm->class;
                curr_depth = task->depth;
                task->depth++;
                err = knd_class_export_JSON(c, task, false, depth + 2);
                RET_ERR();
                task->depth = curr_depth;
                }*/
            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 2) * indent_size);
                RET_ERR();
            }
            OUT("}", 1);
            break;
        default:
            OUT("\"", 1);
            OUT(stm->val, stm->val_size);
            OUT("\"", 1);
        }
    }

    return knd_OK;
}

int knd_attr_stm_export_JSON(struct kndAttrStm *stm, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    size_t indent_size = task->ctx->format_indent;
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndClassRefAttrStm *cref;
    int err;

    // if (task->ctx->depth > task->ctx->max_depth) return knd_OK;

    OUT("\"", 1);
    OUT(stm->name, stm->name_size);
    OUT("\":", strlen("\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    
    switch (stm->attr->type) {
    case KND_ATTR_UINT:
        // fall through
    case KND_ATTR_UREAL:
        OUT(stm->val, stm->val_size);
        break;
    case KND_ATTR_CLASS_REF:
        assert(stm->subtype != NULL);
        cref = stm->subtype;
        entry = cref->cls_entry;

        OUT("\"", 1);
        OUT(entry->name, entry->name_size);
        OUT("\"", 1);

        err = knd_class_acquire(entry, &c, task);
        KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);
        if (c->tr) {
            //err = knd_text_gloss_export_GSL(c->tr, task, depth);
            //KND_TASK_ERR("failed to export gloss GSL");
        }
        break;
    case KND_ATTR_INNER:
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth) * indent_size);
            RET_ERR();
        }
        OUT("{", 1);
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }

        /*if (stm->class) {
            err = knd_class_export_JSON(stm->class, task, false, depth + 2);
            if (err) return err;
            break;
            }*/
        err = inner_stm_export_JSON(stm, task, depth + 1);
        KND_TASK_ERR("failed to export inner stm JSON");
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth) * indent_size);
            RET_ERR();
        }
        OUT("}", 1);
        break;
    case KND_ATTR_TEXT:
        assert(stm->subtype != NULL);
        // OUT("\"_t\":{", strlen("\"_t\":"));
        err = knd_text_export(stm->subtype, KND_FORMAT_JSON, task, depth + 1);
        KND_TASK_ERR("GSL text export failed");
        // OUT("}", strlen("}"));
        break;
    default:
        OUT("\"", 1);
        OUT(stm->val, stm->val_size);
        OUT("\"", 1);
    }
    return knd_OK;
}
