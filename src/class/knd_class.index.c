#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdatomic.h>

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
#include "knd_task.h"
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

#define DEBUG_CLASS_INDEX_LEVEL_1 0
#define DEBUG_CLASS_INDEX_LEVEL_2 0
#define DEBUG_CLASS_INDEX_LEVEL_3 0
#define DEBUG_CLASS_INDEX_LEVEL_4 0
#define DEBUG_CLASS_INDEX_LEVEL_5 0
#define DEBUG_CLASS_INDEX_LEVEL_TMP 1

static int index_ancestor(struct kndClass *cls, struct kndClass *baseclass, struct kndTask *task)
{
    struct kndClassEntry *entry = cls->entry;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *desc_idx;
    void *result;
    int err;

    desc_idx = baseclass->descendants;
    if (!desc_idx) {        
        err = knd_set_new(&desc_idx, KND_SET_STORE_MEMONLY, mempool);
        KND_TASK_ERR("failed to alloc a set");

        baseclass->descendants = desc_idx;
    }
    if (DEBUG_CLASS_INDEX_LEVEL_2) {
        knd_log(".. {cls %.*s {id %.*s}} to update desc_idx of an ancestor {cls %.*s}",
                cls->name_size, cls->name, entry->id_size, entry->id,
                baseclass->name_size, baseclass->name);
    }

    err = knd_set_get(desc_idx, entry->id, entry->id_size, &result, task);
    if (!err) {
        if (DEBUG_CLASS_INDEX_LEVEL_3) {
            knd_log("== idx already present between {cls %.*s {id %.*s}}"
                    " and its ancestor {cls %.*s}",
                    entry->name_size, entry->name, entry->id_size, entry->id,
                    baseclass->name_size, baseclass->name);
        }
        return knd_OK;
    }
    baseclass->num_descendants++;

    /* register as a descendant */
    err = knd_set_add(desc_idx, entry->id, entry->id_size, (void*)entry, task);
    KND_TASK_ERR("failed to register a descendant");

    return knd_OK;
}

static int register_desc(struct kndClass *base, struct kndClass *sub,
                         struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClassRef *ref;
    struct kndClassEntry *entry = sub->entry;
    struct kndClassEntry *match;
    struct kndClass *c;
    struct kndSet *desc_idx;
    int err;

    /* update ancestors' indices */
    FOREACH (ref, base->ancestors) {
        err = knd_class_acquire(ref->entry, &c, snapshot, task);
        KND_TASK_ERR("failed to acquire {cls %.*s}",
                     ref->entry->name_size, ref->entry->name);

        //if (c->state_top) continue;

        err = index_ancestor(sub, c, task);
        KND_TASK_ERR("failed to index ancestor {cls %.*s} of {cls %.*s}",
                     c->name_size, c->name, base->name_size, base->name);
    }

    if (DEBUG_CLASS_INDEX_LEVEL_2) {
        knd_log(".. add {cls %.*s} as a child of {cls %.*s}",
                sub->name_size, sub->name, base->name_size, base->name);
    }

    /* register a descendant */
    desc_idx = base->descendants;
    if (!desc_idx) {
        err = knd_set_new(&desc_idx, KND_SET_STORE_MEMONLY, mempool);
        KND_TASK_ERR("failed to alloc a desc idx set");
        base->descendants = desc_idx;
    } else {
        err = knd_set_get(desc_idx, entry->id, entry->id_size, (void**)&match, task);
        if (!err) {
            //knd_log("-- {desc-idx %p} descendant class already registered? {base %.*s} {match %.*s}",
            //        desc_idx, base->name_size, base->name, match->name_size, match->name);
            return knd_OK;
        }
    }

    err = knd_set_add(desc_idx, entry->id, entry->id_size, (void*)entry, task);
    KND_TASK_ERR("failed to register a descendant {cls %.*s}"
                 " within an ancestor {cls %.*s}  {err %d}",
                 entry->name_size, entry->name, base->name_size, base->name, err);
    base->num_descendants++;

    return knd_OK;
}

static int find_direct_child(struct kndClassEntry *base,
                             struct kndClassEntry *curr_entry,
                             struct kndClassEntry *term,
                             struct kndClassEntry **result, size_t *numval,
                             struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndClass *base_c, *sub_c, *term_c;
    struct kndClassRef *ref;
    bool ff_search = curr_entry != NULL ? true : false;
    int err;

    err = knd_class_acquire(base, &base_c, snapshot, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", base->name_size, base->name);

    if (!base_c->num_children) return knd_NO_MATCH;

    if (DEBUG_CLASS_INDEX_LEVEL_2) {
        knd_log(".. looking for the next child of {cls %.*s}",
                base_c->name_size, base_c->name);
    }

    FOREACH (ref, base_c->children) {
        /* continue matching only after current entry is met */
        if (ff_search) {
            if (ref->entry == curr_entry) {
                ff_search = false;
                continue;
            }
            continue;
        }

        /* terminal class reached */
        if (ref->entry == term) {
            *result = term;
            *numval = ref->numid;
            return knd_OK;
        }

        /* check inheritance */
        err = knd_class_acquire(ref->entry, &sub_c, snapshot, task);
        KND_TASK_ERR("failed to acquire {cls %.*s}",
                     ref->entry->name_size, ref->entry->name);

        err = knd_class_acquire(term, &term_c, snapshot, task);
        KND_TASK_ERR("failed to acquire {cls %.*s}", term->name_size, term->name);

        err = knd_class_is_base(sub_c, term_c);
        if (err) continue;

        if (DEBUG_CLASS_INDEX_LEVEL_3) {
            knd_log("  ++ class inheritance confirmed from {base %.*s} to {term %.*s}",
                    sub_c->name_size, sub_c->name, term->name_size, term->name);
        }
        *result = ref->entry;
        *numval = ref->numid;
        return knd_OK;
    }
    return knd_NO_MATCH;
}

int knd_facet_cls_hash(void *parent_key, void *curr_key, void *term_key,
                       void *ctx, void **result, size_t *numval,
                       struct kndTask *task)
{
    struct kndClassEntry *parent_entry = parent_key;
    struct kndClassEntry *curr_entry = curr_key;
    struct kndClassEntry *term_entry = term_key;
    struct kndClassEntry *entry;
    struct kndRepoSnapshot *snapshot = ctx;
    int err;

    assert (parent_entry != NULL);
    assert (term_entry != NULL);

    if (parent_entry == term_entry) return knd_NO_MATCH;

    err = find_direct_child(parent_entry, curr_entry, term_entry, &entry,
                            numval, snapshot, task);
    switch (err) {
    case knd_OK:
        *result = entry;
        return knd_OK;
    case knd_NO_MATCH:
        return knd_NO_MATCH;
    default:
        KND_TASK_ERR("failed to find a subclass between {cls %.*s} and {cls %.*s}",
                     parent_entry->name_size, parent_entry->name,
                     term_entry->name_size, term_entry->name);
    }
    return knd_NO_MATCH;
}

void knd_facet_cls_key_str(void *key, size_t depth)
{
    struct kndClassEntry *entry = key;

    knd_log("%*s{cls %.*s}",  depth * KND_OFFSET_SIZE, "",
            entry->name_size, entry->name);
    
}

int knd_class_index(struct kndClass *cls, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndClassBasePred *bp;
    struct kndClass *c;
    struct kndAttrStm *stm;
    int err;

    if (DEBUG_CLASS_INDEX_LEVEL_2) {
        knd_log(".. indexing {cls %.*s {id %.*s}}",
                cls->name_size, cls->name, cls->entry->id_size, cls->entry->id);
    }

    FOREACH (bp, cls->base_preds) {
        if (bp->is_root) break;

        err = knd_class_acquire(bp->entry, &c, snapshot, task);
        KND_TASK_ERR("failed to acquire {cls %.*s}", bp->entry->name_size, bp->entry->name);

        err = register_desc(c, cls, snapshot, task);
        KND_TASK_ERR("failed to register a subclass {cls %.*s} in base {cls %.*s}",
                     cls->name_size, cls->name, bp->entry->name_size, bp->entry->name);

        FOREACH (stm, bp->attr_stms) {
            switch (stm->attr->mult_t) {
                case KND_ATTR_MULTIPLE:
                    err = knd_index_attr_stm_list(cls->entry, stm->attr, stm, snapshot, task);
                    KND_TASK_ERR("failed to index {attr-stm-list %.*s}",
                                 stm->attr->name_size, stm->attr->name);
                    break;
            default:
                err = knd_index_attr_stm(cls->entry, stm->attr, stm, snapshot, task);
                KND_TASK_ERR("failed to index {attr-stm %.*s}",
                             stm->attr->name_size, stm->attr->name);
                break;
            }
        }
    }
    cls->phase = KND_CLASS_INDEXED;
    return knd_OK;
}
