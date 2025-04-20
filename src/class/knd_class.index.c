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

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndClass *class;
    struct kndClassBasePred *base_pred;
};

static int index_ancestor(struct kndClass *self, struct kndClass *baseclass, struct kndTask *task)
{
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *prev_entry;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *desc_idx;
    struct kndClass *c;
    struct kndDict *class_name_idx = task->class_name_idx;
    void *result;
    int err;

    if (DEBUG_CLASS_INDEX_LEVEL_2) {
        knd_log(".. %.*s class to update desc_idx of an ancestor {cls %.*s}",
                self->name_size, self->name,
                baseclass->name_size, baseclass->name);
    }

    if (baseclass->entry->repo != entry->repo) {
        prev_entry = knd_dict_get(class_name_idx, baseclass->name, baseclass->name_size);
        if (prev_entry) {
            err = knd_class_acquire(prev_entry, &c, task);
            KND_TASK_ERR("failed to acquire {class %.*s}",
                         prev_entry->name_size, prev_entry->name);

            baseclass = c;
        } else {
            knd_log("-- {class %.*s} not found in {repo %.*s}",
                    baseclass->name_size, baseclass->name,
                    self->entry->repo->name_size, self->entry->repo->name);

            //err = knd_class_clone(base_entry->class,
            //                      self->entry->repo, &base, task);             RET_ERR();
        }
    }

    desc_idx = baseclass->descendants;
    if (!desc_idx) {
        err = knd_set_new(&desc_idx, mempool);
        KND_TASK_ERR("failed to alloc a set");
        desc_idx->type = KND_SET_CLASS;
        //desc_idx->base = baseclass->entry;
        baseclass->descendants = desc_idx;
    }

    err = knd_set_get(desc_idx, entry->id, entry->id_size, &result);
    if (!err) {
        if (DEBUG_CLASS_INDEX_LEVEL_2) {
            knd_log("== index already present between %.*s (%.*s)"
                    " and its ancestor %.*s",
                    entry->name_size, entry->name, entry->id_size, entry->id,
                    baseclass->name_size, baseclass->name);
        }
        return knd_OK;
    }
    baseclass->num_descendants++;

    /* register as a descendant */
    err = knd_set_add(desc_idx, entry->id, entry->id_size, (void*)entry);
    KND_TASK_ERR("failed to register a descendant");

    return knd_OK;
}

static int register_desc(struct kndClass *base, struct kndClass *sub, struct kndTask *task)
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
        err = knd_class_acquire(ref->entry, &c, task);
        KND_TASK_ERR("failed to acquire {class %.*s}",
                     ref->entry->name_size, ref->entry->name);

        if (c->state_top) continue;

        err = index_ancestor(sub, c, task);
        KND_TASK_ERR("failed to index ancestor {class %.*s} of {class %.*s}",
                     c->name_size, c->name, base->name_size, base->name);
    }

    if (DEBUG_CLASS_INDEX_LEVEL_2) {
        knd_log(".. add {class %.*s} as a child of {class %.*s}",
                sub->name_size, sub->name, base->name_size, base->name);
    }

    /* register a descendant */
    desc_idx = base->descendants;
    if (!desc_idx) {
        err = knd_set_new(&desc_idx, mempool);
        KND_TASK_ERR("failed to alloc a desc idx set");
        base->descendants = desc_idx;
    } else {
        err = knd_set_get(desc_idx, entry->id, entry->id_size, (void**)&match);
        if (!err) {
            //knd_log("-- {desc-idx %p} descendant class already registered? {base %.*s} {match %.*s}",
            //        desc_idx, base->name_size, base->name, match->name_size, match->name);
            return knd_OK;
        }
    }

    err = knd_set_add(desc_idx, entry->id, entry->id_size, (void*)entry);
    KND_TASK_ERR("failed to register a descendant {class %.*s}"
                 " within an ancestor {class %.*s}  {err %d}",
                 entry->name_size, entry->name, base->name_size, base->name, err);
    base->num_descendants++;

    return knd_OK;
}

int knd_class_update_indices(struct kndRepo *repo, struct kndClassEntry *self,
                             struct kndState *unused_var(state), struct kndTask *task)
{
    struct kndSharedSet *idx = task->idxs->class_idx;
    //struct kndStateRef *ref;
    //int err;

    knd_log(".. update {repo %.*s {class %.*s}} indices",
            repo->name_size, repo->name, self->name_size, self->name);

    return knd_OK;
}

int knd_class_index(struct kndClass *cls, struct kndTask *task)
{
    struct kndClassBasePred *bp;
    struct kndClass *c;
    struct kndAttrStm *stm;
    int err;

    if (DEBUG_CLASS_INDEX_LEVEL_2) {
        knd_log(".. indexing {cls %.*s {id %.*s}}",
                cls->entry->name_size, cls->entry->name,
                cls->entry->id_size, cls->entry->id);
    }

    FOREACH (bp, cls->base_preds) {
        err = knd_class_acquire(bp->entry, &c, task);
        KND_TASK_ERR("failed to acquire {cls %.*s}", bp->entry->name_size, bp->entry->name);

        err = register_desc(c, cls, task);
        KND_TASK_ERR("failed to register a subclass {cls %.*s} in base {cls %.*s}",
                     cls->name_size, cls->name, bp->entry->name_size, bp->entry->name);

        FOREACH (stm, bp->attr_stms) {
            if (stm->attr->is_a_set) {
                err = knd_index_attr_stm_list(cls->entry, stm->attr, stm, task);
                KND_TASK_ERR("failed to index {attr-stm-list %.*s}",
                             stm->attr->name_size, stm->attr->name);
            } else {
                err = knd_index_attr_stm(cls->entry, stm->attr, stm, task);
                KND_TASK_ERR("failed to index {attr-stm %.*s}",
                             stm->attr->name_size, stm->attr->name);
            }
        }
    }
    cls->phase = KND_CLASS_INDEXED;
    return knd_OK;
}

static int find_direct_child(struct kndClassEntry *base, struct kndClassEntry *term,
                             struct kndClassEntry **result, size_t *numval,
                             struct kndTask *task)
{
    struct kndClass *base_c, *sub_c, *term_c;
    struct kndClassEntry *child;
    struct kndClassRef *ref;
    int err;

    err = knd_class_acquire(base, &base_c, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", base->name_size, base->name);

    if (!base_c->num_children) return knd_NO_MATCH;

    FOREACH (ref, base_c->children) {

        knd_log(">> child {cls %.*s {child-id %zu}}",
                ref->entry->name_size, ref->entry->name, ref->numid);

        if (ref->entry == term) {
            *result = term;
            *numval = ref->numid;
            return knd_OK;
        }

        err = knd_class_acquire(ref->entry, &sub_c, task);
        KND_TASK_ERR("failed to acquire {cls %.*s}",
                     ref->entry->name_size, ref->entry->name);

        err = knd_class_acquire(term, &term_c, task);
        KND_TASK_ERR("failed to acquire {cls %.*s}", term->name_size, term->name);

        err = knd_class_is_base(sub_c, term_c);
        if (err) continue;

        *result = ref->entry;
        *numval = ref->numid;
        return knd_OK;
    }

    return knd_NO_MATCH;
}

int knd_facet_subclass_hash(void *val, void *elem, void **payload, size_t *hashval,
                            struct kndTask *task)
{
    struct kndAttrStm *stm = elem;
    struct kndAttr *attr = stm->is_list_item ? stm->parent->attr : stm->attr;
    struct kndClassInnerAttr *cls_inner_attr;
    struct kndClassRefAttr *cls_ref_attr;

    struct kndClassRefAttrStm *ref_stm;
    struct kndClassInnerAttrStm *inner_stm;

    struct kndClassEntry *entry, *result;
    struct kndClassEntry *curr_entry = val;
    size_t numval;
    int err;

    assert (curr_entry != NULL);

    switch (attr->type) {
    case KND_ATTR_CLS_INNER:
        cls_inner_attr = attr->subtype;
        inner_stm = stm->subtype;

        entry = inner_stm->cls_entry ? inner_stm->cls_entry : cls_inner_attr->template_cls;

        if (DEBUG_CLASS_INDEX_LEVEL_TMP) {
            knd_log(".. subclass hash of {inner %.*s} {facet-cls %.*s}",
                    entry->name_size, entry->name,
                    curr_entry->name_size, curr_entry->name);
        }

        err = find_direct_child(curr_entry, entry, &result, hashval, task);
        KND_TASK_ERR("failed to match a direct child of {cls %.*s}",
                     curr_entry->name_size, curr_entry->name);

        *payload = result;
        // TODO
        return knd_NO_MATCH;
    case KND_ATTR_CLS_REF:
        cls_ref_attr = attr->subtype;
        ref_stm = stm->subtype;
        entry = ref_stm->cls_entry ? ref_stm->cls_entry : cls_ref_attr->template_cls;

        if (DEBUG_CLASS_INDEX_LEVEL_TMP) {
            knd_log(".. subclass hash of {cls-ref %.*s} {facet-cls %.*s}",
                    entry->name_size, entry->name,
                    curr_entry->name_size, curr_entry->name);
        }

        err = find_direct_child(curr_entry, entry, &result, hashval, task);
        KND_TASK_ERR("failed to match a direct child of {cls %.*s}",
                     curr_entry->name_size, curr_entry->name);
        *payload = result;
        // TODO
        return knd_NO_MATCH;
    default:
        break;
    }
    return knd_NO_MATCH;
}

void knd_facet_subclass_str(void *val, size_t depth)
{
    struct kndClassEntry *entry = val;

    knd_log("{cls %.*s}", entry->name_size, entry->name);
}
