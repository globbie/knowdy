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

static int attr_stm_list_export_GSL(struct kndAttrStm *parent_item, struct kndTask *task, size_t depth);

static int inner_stm_export_GSL(struct kndAttrStm *stm, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr = stm->attr;
    struct kndAttrStm *item;
    struct kndClassEntry *entry;
    struct kndClass *c;
    struct kndClassRefAttrStm *cref;
    int err;

    if (DEBUG_ATTR_STM_GSL_LEVEL_2) {
        knd_log(".. GSL export inner stm \"%.*s\" val:%.*s  list item:%d",
                stm->name_size, stm->name, stm->val_size, stm->val, stm->is_list_item);
    }

    FOREACH (item, stm->children) {
        err = knd_attr_stm_export_GSL(item, task, depth);
        KND_TASK_ERR("failed to export inner stm GSL");
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
    struct kndClass   *self = NULL;
    struct kndAttrRef *ref = elem;
    struct kndAttr *attr = ref->attr;
    struct kndAttrStm *attr_stm = ref->attr_stm;
    struct kndOutput *out = task->out;
    struct kndMemPool *mempool = task->mempool;
    size_t numval = 0;
    size_t depth = 1;
    int err;

    if (DEBUG_ATTR_STM_GSL_LEVEL_2)
        knd_log(".. class \"%.*s\" to export inherited attr \"%.*s\"..",
                self->name_size, self->name, attr->name_size, attr->name);

    /* skip over immediate attrs */
    if (attr->owner == self) return knd_OK;

    if (!attr_stm) {
        // TODO
        return knd_OK;
        //err = knd_get_attr_stm(self, attr->name, attr->name_size, &attr_stm);
        //if (err) return knd_OK;
    }

    if (attr->is_a_set) {
        return attr_stm_list_export_GSL(attr_stm, task, depth);
    }

    err = out->writec(out, '{');                                          RET_ERR();
    err = out->write(out, attr_stm->name, attr_stm->name_size);           RET_ERR();

    switch (attr->type) {
    case KND_ATTR_UINT:
        err = out->writec(out, ' ');                            RET_ERR();
        err = out->write(out, attr_stm->val, attr_stm->val_size);             RET_ERR();
        break;
    case KND_ATTR_CLS_INNER:
        err = inner_stm_export_GSL(attr_stm, task, depth + 1);
        if (err) return err;
        break;
    case KND_ATTR_STR:
        err = out->writec(out, ' ');                            RET_ERR();
        err = out->write(out, attr_stm->val, attr_stm->val_size);             RET_ERR();
        break;
    default:
        break;
    }
    err = out->writec(out, '}');                                          RET_ERR();
    
    return knd_OK;
}

static int ref_stm_export_GSL(struct kndAttrStm *stm, struct kndTask *task, size_t unused_var(depth))
{
    struct kndOutput *out = task->out;
    struct kndClassRefAttrStm *cref = stm->subtype;

    assert(cref->cls_entry != NULL);

    OUT(cref->cls_entry->name, cref->cls_entry->name_size);
    return knd_OK;
}

static int attr_stm_list_export_GSL(struct kndAttrStm *stm, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr = stm->attr;
    struct kndAttrStm *item;
    size_t count = 0;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (DEBUG_ATTR_STM_GSL_LEVEL_2)
        knd_log(".. export GSL list attr \"%.*s\"", stm->name_size, stm->name);

    OUT("[", 1);
    OUT(stm->name, stm->name_size);

    FOREACH (item, stm->list) {
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        OUT("{", 1);
        switch (attr->type) {
        case KND_ATTR_CLS_INNER:
            item->id_size = sprintf(item->id, "%lu", (unsigned long)count);
            count++;
            err = inner_stm_export_GSL(item, task, depth + 2);
            if (err) return err;
            break;
        case KND_ATTR_CLS_REF:
            err = ref_stm_export_GSL(item, task, depth + 2);
            if (err) return err;
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

int knd_attr_stms_export_GSL(struct kndAttrStm *stms, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttrStm *stm;
    struct kndAttr *attr;
    size_t curr_depth = task->ctx->depth;
    size_t indent_size = task->ctx->format_indent;
    size_t count = 0;
    int err;

    FOREACH (stm, stms) {
        attr = stm->attr;

        if (!attr) {
            knd_log("-- no attr found for {attr-stm %.*s}",
                    stm->name_size, stm->name);
            continue;
        }

        if (DEBUG_ATTR_STM_GSL_LEVEL_3) {
            knd_log(">> attr stm GSL export: %.*s",
                    attr->name_size, attr->name);
        }
        task->ctx->depth = curr_depth;

        if (indent_size && count) {
            OUT("\n", 1);
            err = knd_print_offset(out, depth * indent_size);
            KND_TASK_ERR("offset output failed");
        }
        count++;

        if (attr->is_a_set) {
            err = attr_stm_list_export_GSL(stm, task, depth);
            KND_TASK_ERR("attr stm list GSL export failed");
            continue;
        }
        err = knd_attr_stm_export_GSL(stm, task, depth + 1);
        KND_TASK_ERR("attr stm GSL export failed");
    }
    return knd_OK;
}

int knd_attr_stm_export_GSL(struct kndAttrStm *stm, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr = stm->attr;
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndClassRefAttrStm *cref;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (!attr) {
        knd_log("no attr for {attr-stm %.*s}", stm->name_size, stm->name);
        return knd_FAIL;
    }

    if (task->ctx->depth >= task->ctx->max_depth) {
        if (DEBUG_ATTR_STM_GSL_LEVEL_3) {
            knd_log("NB: max depth reached: %zu", task->ctx->depth);
        }
        return knd_OK;
    }

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        KND_TASK_ERR("GSL offset output failed");
    }

    if (stm->is_list_item) {
        OUT("{", 1);
    } else {
        if (!attr->is_a_set) {
            OUT("{", 1);
            OUT(stm->name, stm->name_size);
            OUT(" ", 1);
        }
    }

    switch (stm->attr->type) {
    case KND_ATTR_UINT:
        // fall through
    case KND_ATTR_UREAL:
        OUT(stm->val, stm->val_size);
        break;
    case KND_ATTR_CLS_REF:
        assert(stm->subtype != NULL);
        cref = stm->subtype;
        entry = cref->cls_entry;

        OUT(entry->name, entry->name_size);

        err = knd_class_acquire(entry, &c, task);
        KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);
        if (c->tr) {
            err = knd_text_gloss_export_GSL(c->tr, true, task, depth + 1);
            KND_TASK_ERR("failed to export gloss GSL");
        }
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

    if (stm->is_list_item || !attr->is_a_set) {
        OUT("}", 1);
    } 
    return knd_OK;
}
