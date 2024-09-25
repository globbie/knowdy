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
                           struct kndAttrStm *var, struct kndTask *task)
{
    struct kndAttrStm *item;
    int err;

    if (DEBUG_ATTR_STM_IDX_LEVEL_2) {
        knd_log("?? indexing check for {class %.*s} inner attr {%s %.*s} {is-a-set %d}",
                topic->name_size, topic->name,
                knd_attr_names[attr->type], attr->name_size, attr->name,
                attr->is_a_set);
    }

    if (var->implied_attr && var->implied_attr->is_indexed) {
        err = knd_index_attr_stm(topic, var->implied_attr, var, task);
        KND_TASK_ERR("failed to index attr var %.*s",
                     var->implied_attr->name_size, var->implied_attr->name);
    }

    /* check nested children */
    FOREACH (item, var->children) {
        if (item->attr->is_a_set) {
            err = knd_index_attr_stm_list(topic, item->attr, item, task);
            KND_TASK_ERR("failed to index attr var list %.*s",
                         item->attr->name_size, item->attr->name);
            return knd_OK;
        }

        err = knd_index_attr_stm(topic, item->attr, item, task);
        KND_TASK_ERR("failed to index attr var %.*s",
                     item->attr->name_size, item->attr->name);
    }
    return knd_OK;
}

int knd_index_attr_stm(struct kndClassEntry *topic, struct kndAttr *attr,
                       struct kndAttrStm *stm, struct kndTask *task)
{
    struct kndAttrFacet *facet = attr->facets;
    int err;

    if (DEBUG_ATTR_STM_IDX_LEVEL_2) {
        knd_log(".. {class %.*s} to index {%s %.*s {%.*s %.*s}}",
                topic->name_size, topic->name,
                knd_attr_names[attr->type], attr->name_size, attr->name,
                stm->name_size, stm->name, stm->val_size, stm->val);
    }

    if (!facet) {
        err = knd_attr_facet_new(&facet, task->mempool);
        KND_TASK_ERR("failed to alloc attr facet");
        attr->facets = facet;
        attr->num_facets = 1;
    }

    switch (attr->type) {
    case KND_ATTR_UINT:
        facet->type = KND_ATTR_FACET_SEQ_SIZE;

        err = knd_quant_uint_index(facet, topic, stm, task);
        KND_TASK_ERR("failed to index natural number attr stm");
        break;
    case KND_ATTR_URATIO:
        facet->type = KND_ATTR_FACET_SEQ_SIZE;
        //err = knd_quant_uint_index(attr->impl, topic, stm, task);
        //KND_TASK_ERR("failed to index natural number attr stm");
        break;
    case KND_ATTR_UREAL:
        facet->type = KND_ATTR_FACET_SEQ_SIZE;
        err = knd_quant_ureal_index(facet, topic, stm, task);
        KND_TASK_ERR("failed to index real number attr stm");
        break;
    case KND_ATTR_STR:
        if (DEBUG_ATTR_STM_IDX_LEVEL_TMP) {
            knd_log(".. {class %.*s} to index charseq attr {%s %.*s {%.*s %.*s}}",
                    topic->name_size, topic->name,
                    knd_attr_names[attr->type], attr->name_size, attr->name,
                    stm->name_size, stm->name, stm->val_size, stm->val);
        }
        break;
    case KND_ATTR_REF:
        if (DEBUG_ATTR_STM_IDX_LEVEL_TMP) {
            knd_log(".. {class %.*s} to index classref attr {%s %.*s {%.*s %.*s}}",
                    topic->name_size, topic->name,
                    knd_attr_names[attr->type], attr->name_size, attr->name,
                    stm->name_size, stm->name, stm->val_size, stm->val);
        }
        //err = knd_classref_index(topic, attr, stm, task);
        //KND_TASK_ERR("failed to index ref attr stm");
        break;
    case KND_ATTR_INNER:
        if (DEBUG_ATTR_STM_IDX_LEVEL_3) {
            knd_log(".. {class %.*s} to index inner class attr {%s %.*s {%.*s %.*s}}",
                    topic->name_size, topic->name,
                    knd_attr_names[attr->type], attr->name_size, attr->name,
                    stm->name_size, stm->name, stm->val_size, stm->val);
        }
        //err = knd_attr_stm_inner_idx(topic, attr, stm, task);
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
    case KND_ATTR_INNER:
        //
        break;
    case KND_ATTR_REL:
        // err = knd_attr_pred_index(topic_inst, attr, stm, task);
        // KND_TASK_ERR("failed to index inner attr stm");
        break;
    case KND_ATTR_REF:
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

