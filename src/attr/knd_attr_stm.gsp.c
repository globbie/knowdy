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
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#define DEBUG_ATTR_STM_GSP_LEVEL_1 0
#define DEBUG_ATTR_STM_GSP_LEVEL_2 0
#define DEBUG_ATTR_STM_GSP_LEVEL_3 0
#define DEBUG_ATTR_STM_GSP_LEVEL_4 0
#define DEBUG_ATTR_STM_GSP_LEVEL_5 0
#define DEBUG_ATTR_STM_GSP_LEVEL_TMP 1

struct LocalContext {
    struct kndClassBasePred *base_pred;
    struct kndAttrStm  *list_parent;
    struct kndSet      *attr_idx;
    struct kndAttr     *attr;
    struct kndAttrStm  *attr_stm;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

static int attr_stm_list_export_GSP(struct kndAttrStm *parent_item, struct kndTask *task, struct kndOutput *out);

static int inner_attr_export_GSP(struct kndAttrStm *stm, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndAttrStm *item;
    struct kndAttr *attr;
    struct kndCharSeq *seq;
    struct kndClassEntry *entry;
    struct kndClassRefAttrStm *cref;
    struct kndClassInnerAttrStm *inner_stm = stm->subtype;
    int err;

    /* specific subclass */
    entry = inner_stm->cls_entry;
    if (entry) {
        OUT(entry->id, entry->id_size);
    }

    FOREACH (item, stm->children) {
        attr = item->attr;
        if (attr->is_a_set) {
            err = attr_stm_list_export_GSP(item, task, out);
            KND_TASK_ERR("failed to export inner attr stm list");
            continue;
        }

        OUT("{", 1);
        OUT(attr->id, attr->id_size);
        OUT(" ", 1);

        switch (attr->type) {
        case KND_ATTR_CLS_REF:
            cref = item->subtype;
            entry = cref->cls_entry;
            OUT(entry->id, entry->id_size);
            break;
        case KND_ATTR_TEXT:
            OUT("{_t ", strlen("{_t "));
            err = knd_text_export_GSP(item->subtype, task);
            KND_TASK_ERR("failed to export text GSP");
            OUT("}", 1);
            break;
        case KND_ATTR_CLS_INNER:
            err = inner_attr_export_GSP(item, task);
            KND_TASK_ERR("failed to export inner stm GSP");
            break;
        case KND_ATTR_BOOL:
            OUT("t", 1);
            break;
        default:
            assert(item->val != NULL);
            assert(item->val_size != 0);
            err = knd_charseq_fetch(item->val, item->val_size, &seq, task);
            KND_TASK_ERR("failed to encode a charseq");
            OUT(seq->id, seq->id_size);
            break;
        }
        OUT("}", 1);
    }
    return knd_OK;
}

static int attr_stm_list_export_GSP(struct kndAttrStm *stm, struct kndTask *task,
                                    struct kndOutput *out)
{
    struct kndCharSeq *seq;
    struct kndAttrStm *item;
    struct kndClassEntry *entry;
    struct kndClassRefAttrStm *cref;

    assert(stm->attr != NULL);
    knd_attr_type attr_type = stm->attr->type;
    int err;

    if (DEBUG_ATTR_STM_GSP_LEVEL_3) {
        knd_log(".. export GSP list: %.*s", stm->name_size, stm->name);
    }

    OUT("[", 1);
    OUT(stm->attr->id, stm->attr->id_size);

    FOREACH (item, stm->list) {
        OUT("{", 1);
        switch (attr_type) {
        case KND_ATTR_CLS_REF:
            assert(item->subtype != NULL);
            cref = item->subtype;
            entry = cref->cls_entry;

            OUT(entry->id, entry->id_size);
            break;
        case KND_ATTR_TEXT:
            OUT("{_t ", strlen("{_t "));
            err = knd_text_export_GSP(item->subtype, task);
            KND_TASK_ERR("failed to export text GSP");
            OUT("}", 1);
            break;
        case KND_ATTR_CLS_INNER:
            err = inner_attr_export_GSP(item, task);
            KND_TASK_ERR("failed to export inner attr stm");
            break;
        case KND_ATTR_STR:
            assert (item->val != NULL);
            assert (item->val_size != 0);
            err = knd_charseq_fetch(item->val, item->val_size, &seq, task);
            KND_TASK_ERR("failed to encode a charseq");
            OUT(seq->id, seq->id_size);
            break;
        default:
            if (item->val_size) {
                OUT(item->val, item->val_size);
                err = knd_attr_stm_export_GSP(item, task, out, 0);
                KND_TASK_ERR("failed to export attr stm");
            }
            break;
        }
        OUT("}", 1);
    }
    OUT("]", 1);
    return knd_OK;
}

int knd_attr_stms_export_GSP(struct kndAttrStm *items, struct kndOutput *out,
                             struct kndTask *task, size_t unused_var(depth))
{
    struct kndAttrStm *item;
    struct kndAttr *attr;
    int err;

    FOREACH (item, items) {
        if (!item->attr) continue;
        attr = item->attr;

        if (attr->is_a_set) {
            err = attr_stm_list_export_GSP(item, task, out);
            KND_TASK_ERR("failed to export attr var list");
            continue;
        }
        OUT("{", 1);
        OUT(attr->id, attr->id_size);
        OUT(" ", 1);
        err = knd_attr_stm_export_GSP(item, task, out, 0);
        KND_TASK_ERR("failed to export attr var");
        OUT("}", 1);
    }
    return knd_OK;
}

int knd_attr_stm_export_GSP(struct kndAttrStm *stm, struct kndTask *task,
                            struct kndOutput *out, size_t unused_var(depth))
{
    struct kndCharSeq *seq;
    struct kndClassEntry *entry;
    struct kndClassRefAttrStm *cref;
    int err;

    assert(stm->attr != NULL);
    knd_attr_type attr_type = stm->attr->type;

    switch (attr_type) {
    case KND_ATTR_CLS_REF:
        cref = stm->subtype;
        entry = cref->cls_entry;
        OUT(entry->id, entry->id_size);
        break;
    case KND_ATTR_CLS_INNER:
        err = inner_attr_export_GSP(stm, task);
        KND_TASK_ERR("failed to export inner stm GSP");
        break;
    case KND_ATTR_TEXT:
        OUT("{_t ", strlen("{_t "));
        err = knd_text_export_GSP(stm->subtype, task);
        KND_TASK_ERR("GSP text export failed");
        OUT("}", 1);
        break;
    default:
        if (stm->val_size) {
            err = knd_charseq_fetch(stm->val, stm->val_size, &seq, task);
            KND_TASK_ERR("failed to encode a charseq");
            OUT(seq->id, seq->id_size);
        }
        break;
    }
    return knd_OK;
}

int knd_attr_stm_subj_GSP(void *obj, void *unused_var(ctx),
                          struct kndStorageLeaf *unused_var(leaf),
                          size_t *result_size, struct kndTask *unused_var(task))
{
    struct kndAttrStm *stm = obj;
    struct kndClass *c = stm->subj;
    assert (c != NULL);

    knd_log("** {cls %.*s {%.*s %.*s}}",
            c->name_size, c->name, stm->name_size, stm->name, stm->val_size, stm->val);

    *result_size = 0;

    return knd_OK;
}
