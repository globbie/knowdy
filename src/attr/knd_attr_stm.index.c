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
#include "knd_facet.h"
#include "knd_quant.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_STM_IDX_LEVEL_1 0
#define DEBUG_ATTR_STM_IDX_LEVEL_2 0
#define DEBUG_ATTR_STM_IDX_LEVEL_3 0
#define DEBUG_ATTR_STM_IDX_LEVEL_4 0
#define DEBUG_ATTR_STM_IDX_LEVEL_5 0
#define DEBUG_ATTR_STM_IDX_LEVEL_TMP 1

int knd_attr_stm_inner_idx(struct kndClassEntry *topic, struct kndAttr *attr,
                           struct kndAttrStm *parent, struct kndTask *task)
{
    struct kndAttrStm *item;
    int err;

    if (DEBUG_ATTR_STM_IDX_LEVEL_2) {
        knd_log("?? indexing check for {class %.*s} inner attr {%s %.*s} {is-a-set %d}",
                topic->name_size, topic->name,
                knd_attr_names[attr->type], attr->name_size, attr->name,
                attr->is_a_set);
    }

    if (parent->implied_attr && parent->implied_attr->is_indexed) {
        err = knd_index_attr_stm(topic, parent->implied_attr, parent, task);
        KND_TASK_ERR("failed to index attr stm %.*s",
                     parent->implied_attr->name_size, parent->implied_attr->name);
    }

    /* check nested children */
    FOREACH (item, parent->children) {
        if (item->attr->is_a_set) {
            err = knd_index_attr_stm_list(topic, item->attr, item, task);
            KND_TASK_ERR("failed to index attr stm list %.*s",
                         item->attr->name_size, item->attr->name);
        } else {
            err = knd_index_attr_stm(topic, item->attr, item, task);
            KND_TASK_ERR("failed to index attr stm %.*s",
                         item->attr->name_size, item->attr->name);
        }
    }
    return knd_OK;
}

int knd_index_attr_stm(struct kndClassEntry *entry, struct kndAttr *attr,
                       struct kndAttrStm *stm, struct kndTask *task)
{
    struct kndQuantAttr *quant_attr;
    struct kndClassRefAttr *cls_ref_attr;
    struct kndClassRefAttrStm *cref;
    struct kndQuantAttrStm *quant_attr_stm;
    int err;

    if (DEBUG_ATTR_STM_IDX_LEVEL_2) {
        knd_log(".. {class %.*s} to index {%s %.*s {%.*s %.*s}}",
                entry->name_size, entry->name,
                knd_attr_names[attr->type], attr->name_size, attr->name,
                stm->name_size, stm->name, stm->val_size, stm->val);
    }

    switch (attr->type) {
    case KND_ATTR_UINT:
        quant_attr = attr->subtype;
        quant_attr_stm = stm->subtype;
        if (!attr->facet) {
            err = knd_facet_new(&attr->facet,
                                quant_attr->hash_specs, quant_attr->num_hash_specs, task->mempool);
            KND_TASK_ERR("failed to alloc a facet");
        }
        err = knd_facet_add(attr->facet, (void*)quant_attr_stm->uint, task);
        KND_TASK_ERR("failed to add {uint} elem to facet");
        break;
    case KND_ATTR_URATIO:
        //err = knd_quant_uint_index(attr->impl, entry, stm, task);
        //KND_TASK_ERR("failed to index natural number attr stm");
        break;
    case KND_ATTR_UREAL:
        //err = knd_quant_ureal_index(facet, entry, stm, task);
        //KND_TASK_ERR("failed to index real number attr stm");
        break;
    case KND_ATTR_STR:
        if (DEBUG_ATTR_STM_IDX_LEVEL_3) {
            knd_log(".. {class %.*s} to index charseq attr {%s %.*s {%.*s %.*s}}",
                    entry->name_size, entry->name,
                    knd_attr_names[attr->type], attr->name_size, attr->name,
                    stm->name_size, stm->name, stm->val_size, stm->val);
        }
        break;
    case KND_ATTR_CLASS_REF:
        cls_ref_attr = attr->subtype;
        if (!attr->facet) {
            err = knd_facet_new(&attr->facet,
                                cls_ref_attr->hash_specs, cls_ref_attr->num_hash_specs, task->mempool);
            KND_TASK_ERR("failed to alloc a facet");
        }
        cref = stm->subtype;

        knd_log(".. indexing {cls-ref %.*s}",
                cref->cls_entry->name_size, cref->cls_entry->name);

        err = knd_facet_add(attr->facet, (void*)cref->cls_entry, task);
        KND_TASK_ERR("failed to add {cls-ref %.*s} elem to facet",
                     cref->cls_entry->name_size, cref->cls_entry->name);
        break;
    case KND_ATTR_INNER:
        if (DEBUG_ATTR_STM_IDX_LEVEL_2) {
            knd_log(".. {class %.*s} to index inner class attr {%s %.*s {%.*s %.*s}}",
                    entry->name_size, entry->name,
                    knd_attr_names[attr->type], attr->name_size, attr->name,
                    stm->name_size, stm->name, stm->val_size, stm->val);
        }
        //err = knd_attr_stm_inner_idx(entry, attr, stm, task);
        //KND_TASK_ERR("failed to index inner attr stm");
        break;
    default:
        break;
    }
    return knd_OK;
}

int knd_index_inst_attr_stm(struct kndClassInstEntry *topic_inst, struct kndAttr *attr,
                            struct kndAttrStm *unused_var(stm), struct kndTask *unused_var(task))
{
    if (DEBUG_ATTR_STM_IDX_LEVEL_TMP) {
        knd_log(".. {class %.*s {inst %.*s}} to index {%s %.*s}",
                topic_inst->is_a->name_size, topic_inst->is_a->name,
                topic_inst->name_size, topic_inst->name,
                knd_attr_names[attr->type], attr->name_size, attr->name);
    }
    switch (attr->type) {
    case KND_ATTR_CLASS_INST_REF:
        //err = index_inst_ref(topic_inst, attr, stm, task);
        //KND_TASK_ERR("failed to index inner attr stm");
        break;
        /*case KND_ATTR_INNER:
        err = index_inner_attr_stm(topic, attr, stm, task);
        KND_TASK_ERR("failed to index inner attr stm");
        break;*/
    default:
        break;
    }
    return knd_OK;
}

int knd_index_attr_stm_list(struct kndClassEntry *topic, struct kndAttr *attr,
                            struct kndAttrStm *parent, struct kndTask *task)
{
    struct kndAttrStm *stm;
    int err;

    if (DEBUG_ATTR_STM_IDX_LEVEL_2) {
        knd_log(".. attr stm list indexing {class %.*s {attr %.*s} [type:%d]}",
                topic->name_size, topic->name, attr->name_size, attr->name, attr->type);
    }
    FOREACH (stm, parent->list) {        
        err = knd_index_attr_stm(topic, attr, stm, task);
        KND_TASK_ERR("failed to index list attr stm %.*s", attr->name_size, attr->name);
    }
    return knd_OK;
}
