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
#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_quant.h"
#include "knd_task.h"
#include "knd_dict.h"
#include "knd_shared_dict.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_STM_RESOLVE_LEVEL_1 0
#define DEBUG_ATTR_STM_RESOLVE_LEVEL_2 0
#define DEBUG_ATTR_STM_RESOLVE_LEVEL_3 0
#define DEBUG_ATTR_STM_RESOLVE_LEVEL_4 0
#define DEBUG_ATTR_STM_RESOLVE_LEVEL_5 0
#define DEBUG_ATTR_STM_RESOLVE_LEVEL_TMP 1

static int resolve_cls_ref(struct kndAttrStm *stm, struct kndRepoSnapshot *snapshot, struct kndTask *task);

static int resolve_inner_cls(struct kndAttrStm *stm, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndClass *template_c;
    struct kndClass *c;
    struct kndAttrStm *item;
    struct kndAttr *attr = stm->attr;
    struct kndClassInnerAttr *cls_inner_attr;
    struct kndClassInnerAttrStm *inner_stm;
    int err;

    err = knd_cls_inner_attr_stm_new(&inner_stm, task->mempool);
    KND_TASK_ERR("failed to alloc {cls-inner %.*s}", stm->val_size, stm->val);
    stm->subtype = inner_stm;

    assert (attr->type == KND_ATTR_CLS_INNER);
    assert (attr->subtype != NULL);

    /* default class template */
    cls_inner_attr = stm->attr->subtype;
    entry = cls_inner_attr->template_cls;

    err = knd_class_acquire(entry, &template_c, snapshot, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);

    if (template_c->phase < KND_CLASS_RESOLVED) {
        err = knd_class_resolve(template_c, snapshot, task);
        KND_TASK_ERR("failed to resolve {cls %.*s}", entry->name_size, entry->name);
    }

    c = template_c;

    if (DEBUG_ATTR_STM_RESOLVE_LEVEL_3) {
        knd_log(".. resolving inner {cls %.*s {stm-val %.*s}}",
                c->name_size, c->name, stm->val_size, stm->val);
    }

    /* explicit subclass is present */
    if (stm->val_size) {
        err = knd_get_cls_by_name(snapshot, stm->val, stm->val_size, &c, task);
        KND_TASK_ERR("no such {cls %.*s}", stm->val_size, stm->val);

        if (c->phase < KND_CLASS_RESOLVED) {
            err = knd_class_resolve(c, snapshot, task);
            KND_TASK_ERR("{cls %.*s} failed to resolve", c->name_size, c->name);
        }

        err = knd_class_is_base(template_c, c);
        KND_TASK_ERR("no inheritance from {cls %.*s} to {cls %.*s}",
                     template_c->name_size, template_c->name, c->name_size, c->name);

        inner_stm->cls_entry = entry;
    }

    FOREACH (item, stm->children) {
        if (item->phase < KND_ATTR_STM_RESOLVED) {
            err = knd_resolve_attr_stm(c, item, snapshot, task);
            KND_TASK_ERR("failed to resolve attr stm {cls %.*s {%.*s}}",
                         c->name_size, c->name, item->name_size, item->name);
        }
    }
    return knd_OK;
}

static int resolve_cls_ref(struct kndAttrStm *stm, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndClass *c, *ref_c;
    struct kndClassEntry *entry;
    struct kndClassRefAttr *cls_ref_attr;
    struct kndClassRefAttrStm *cls_ref_stm;
    int err;

    assert (stm->val != NULL);
    assert (stm->val_size != 0);

    err = knd_cls_ref_attr_stm_new(&cls_ref_stm, task->mempool);
    KND_TASK_ERR("failed to alloc {cls-ref %.*s}", stm->val_size, stm->val);
    stm->subtype = cls_ref_stm;

    cls_ref_attr = stm->attr->subtype;
    entry = cls_ref_attr->template_cls;

    err = knd_class_acquire(entry, &c, snapshot, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);

    if (c->phase < KND_CLASS_RESOLVED) {
        err = knd_class_resolve(c, snapshot, task);
        KND_TASK_ERR("failed to resolve {cls %.*s}", c->name_size, c->name);
    }

    err = knd_resolve_cls_ref(stm->val, stm->val_size, c, &ref_c, snapshot, task);
    KND_TASK_ERR("failed to resolve {cls-ref %.*s}", stm->val_size,  stm->val);

    cls_ref_stm->cls_entry = ref_c->entry;

    return knd_OK;
}

int knd_resolve_attr_stm(struct kndClass *cls, struct kndAttrStm *stm,
                         struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndAttrStm *item;
    struct kndQuantAttrStm *quant_attr_stm;
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    struct kndQuantUInt *uint;
    struct kndQuantUReal *ureal;
    int err;

    if (stm->phase >= KND_ATTR_STM_RESOLVE_IN_PROGRESS) {
        err = knd_CONFLICT;
        KND_TASK_ERR("vicious circle detected while resolving attr {stm %.*s}",
                     stm->name_size, stm->name);
    }

    stm->phase = KND_ATTR_STM_RESOLVE_IN_PROGRESS;

    if (stm->is_list_item) {
        attr = stm->attr;

        assert (attr != NULL);
        if (DEBUG_ATTR_STM_RESOLVE_LEVEL_3) {
            knd_log(".. resolving a list item of {cls %.*s {%s %.*s}}",
                    cls->name_size, cls->name, knd_attr_names[attr->type],
                    attr->name_size, attr->name);
        }
    } else {
        assert (stm->name_size != 0);

        err = knd_class_get_attr(cls, stm->name, stm->name_size, &attr_ref, task);
        KND_TASK_ERR("no {attr %.*s} in {cls %.*s}",
                     stm->name_size, stm->name, cls->name_size, cls->name);
        attr = attr_ref->attr;
    
        attr_ref->attr_stm = stm;
        stm->attr = attr;

        if (DEBUG_ATTR_STM_RESOLVE_LEVEL_3) {
            knd_log(".. resolving {cls %.*s {%s %.*s}}",
                    cls->name_size, cls->name, knd_attr_names[attr->type],
                    stm->name_size, stm->name);
        }

        switch (attr->mult_t) {
        case KND_ATTR_MULTIPLE:
            FOREACH (item, stm->list) {
                item->attr = attr;
                err = knd_resolve_attr_stm(cls, item, snapshot, task);
                KND_TASK_ERR("failed to resolve attr stm {cls %.*s {%.*s}}",
                             cls->name_size, cls->name, stm->name_size, stm->name);
            }
            return knd_OK;
        default:
            break;
        }
    }

    switch (attr->type) {
    case KND_ATTR_CLS_INNER:
        err = resolve_inner_cls(stm, snapshot, task);
        KND_TASK_ERR("failed to resolve an inner {cls %.*s}", stm->val_size, stm->val);
        break;
    case KND_ATTR_CLS_REF:
        err = resolve_cls_ref(stm, snapshot, task);
        KND_TASK_ERR("failed to resolve {cls-ref %.*s}", stm->val_size, stm->val);
        break;
    case KND_ATTR_PROC_REF:
        //proc = attr->proc;
        //err = knd_resolve_proc_ref(stm->val, stm->val_size, proc, &stm->proc_entry, task);
        //KND_TASK_ERR("failed to resolve a proc ref");
        break;
    case KND_ATTR_TEXT:
        err = knd_text_resolve(stm, snapshot, task);
        KND_TASK_ERR("failed to resolve a text attr");
        break;
    case KND_ATTR_UINT:
        assert (stm->val != NULL && stm->val_size != 0);
        
        err = knd_quant_parse_uint(stm->val, stm->val_size, &uint, task);
        KND_TASK_ERR("failed to parse an uint value");
        
        err = knd_quant_attr_stm_new(&quant_attr_stm, task->mempool);
        KND_TASK_ERR("failed to alloc a quant attr stm");
        
        quant_attr_stm->uint = uint;
        
        stm->subtype = quant_attr_stm;
        break;
    case KND_ATTR_UREAL:
        err = knd_quant_parse_ureal(stm->val, stm->val_size, &ureal, task);
        KND_TASK_ERR("failed to parse an ureal value");
        break;
    case KND_ATTR_STR:
        /* TODO: call a validation callback function? */
        assert (stm->val != NULL);
        assert (stm->val_size != 0);
        err = knd_charseq_register(snapshot, stm->val, stm->val_size, &stm->seq, task);
        KND_TASK_ERR("failed to encode a charseq for attr stm val");
        break;
    default:
        break;
    }

    stm->phase = KND_ATTR_STM_RESOLVED;
    return knd_OK;
}
