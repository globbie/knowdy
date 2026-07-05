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

#define DEBUG_ATTR_STM_GSL_LEVEL_1 0
#define DEBUG_ATTR_STM_GSL_LEVEL_2 0
#define DEBUG_ATTR_STM_GSL_LEVEL_3 0
#define DEBUG_ATTR_STM_GSL_LEVEL_4 0
#define DEBUG_ATTR_STM_GSL_LEVEL_5 0
#define DEBUG_ATTR_STM_GSL_LEVEL_TMP 1

static int attr_stm_list_export_GSL(struct kndAttrStm *parent_item,
                                    struct kndTask *task, size_t depth);

static int inner_stm_export_GSL(struct kndAttrStm *stm, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassInnerAttrStm *inner_stm = stm->subtype;
    struct kndClassEntry *entry = inner_stm->cls_entry;
    int err;

    /* specific cls overriding template cls */

    if (entry) {
        OUT(" ", 1);
        OUT(entry->name, entry->name_size);
    }

    err = knd_attr_stms_export_GSL(stm->children, task, depth + 1);
    KND_TASK_ERR("failed to export attr stms GSL");

    return knd_OK;
}

static int attr_stm_list_export_GSL(struct kndAttrStm *stm,
                                    struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttrStm *item;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (DEBUG_ATTR_STM_GSL_LEVEL_2) {
        knd_log(".. export GSL list {attr %.*s}", stm->name_size, stm->name);
    }
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_indent(out, depth * indent_size);
        KND_TASK_ERR("indent output failed");
    }

    OUT("[", 1);
    OUT(stm->name, stm->name_size);

    FOREACH (item, stm->list) {
        err = knd_attr_stm_export_GSL(item, task, depth + 1);
        KND_TASK_ERR("attr stm GSL export failed");
    }
    OUT("]", 1);

    return knd_OK;
}

int knd_attr_stms_export_GSL(struct kndAttrStm *stms,
                             struct kndTask *task, size_t depth)
{
    struct kndAttrStm *stm;
    struct kndAttr *attr;
    int err;

    FOREACH (stm, stms) {
        attr = stm->attr;
        assert (attr != NULL);

        switch (attr->mult_t) {
        case KND_ATTR_MULTIPLE:
            err = attr_stm_list_export_GSL(stm, task, depth);
            KND_TASK_ERR("attr stm list GSL export failed");
            continue;
        default:
            break;
        }

        err = knd_attr_stm_export_GSL(stm, task, depth);
        KND_TASK_ERR("attr stm GSL export failed");
    }
    return knd_OK;
}

int knd_attr_stm_export_GSL(struct kndAttrStm *stm, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr = stm->attr;
    assert (attr != NULL);

    struct kndClassEntry *entry;
    struct kndClassRefAttrStm *cref;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (DEBUG_ATTR_STM_GSL_LEVEL_3) {
        knd_log(">> export GSL {stm %.*s {val %.*s} {val-size %zu}} {is-list-item %d}",
                stm->name_size, stm->name,
                stm->val_size, stm->val, stm->val_size, stm->is_list_item);
    }

    if (task->ctx->depth >= task->ctx->max_depth) {
        if (DEBUG_ATTR_STM_GSL_LEVEL_3) {
            knd_log("NB: max depth reached: %zu", task->ctx->depth);
        }
        return knd_OK;
    }

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_indent(out, depth * indent_size);
        KND_TASK_ERR("GSL indent output failed");
    }

    if (stm->is_list_item) {
        OUT("{", 1);
    } else {
        switch (attr->mult_t) {
        case KND_ATTR_SINGLE:
            OUT("{", 1);
            OUT(stm->name, stm->name_size);
            OUT(" ", 1);
            break;
        default:
            break;
        }
    }

    switch (stm->attr->type) {
    case KND_ATTR_STR:
        OUT(stm->val, stm->val_size);
        break;
    case KND_ATTR_UINT:
        OUT(stm->val, stm->val_size);
        break;
    case KND_ATTR_UREAL:
        OUT(stm->val, stm->val_size);
        break;
    case KND_ATTR_CLS_REF:
        assert(stm->subtype != NULL);
        cref = stm->subtype;
        entry = cref->cls_entry;

        OUT(entry->name, entry->name_size);

        err = knd_text_glosses_export_GSL(entry->glosses, task, depth + 1);
        KND_TASK_ERR("failed to export glosses GSL");
        break;
    case KND_ATTR_CLS_INNER:
        err = inner_stm_export_GSL(stm, task, depth);
        KND_TASK_ERR("GSL inner stm output failed");
        break;
    case KND_ATTR_TEXT:
        assert(stm->subtype != NULL);

        err = knd_text_export(stm->subtype, KND_FORMAT_GSL, task, depth + 1);
        KND_TASK_ERR("GSL text export failed");
        break;
    case KND_ATTR_BOOL:
        OUT("t", 1);
        break;
    default:
        OUT(stm->val, stm->val_size);
        break;
    }

    if (stm->is_list_item) {
        OUT("}", 1);
    } else {
        switch (attr->mult_t) {
        case KND_ATTR_SINGLE:
            OUT("}", 1);
            break;
        default:
            break;
        }
    }

    return knd_OK;
}
