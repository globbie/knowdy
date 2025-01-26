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
        knd_log(".. %.*s class to update desc_idx of an ancestor: \"%.*s\" top:%d",
                self->name_size, self->name,
                baseclass->name_size, baseclass->name, baseclass->state_top);
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
        desc_idx->base = baseclass->entry;
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

static inline void set_child_ref(struct kndClass *self, struct kndClassRef *child_ref)
{
    child_ref->next = self->children;
    self->children = child_ref;
    self->num_children++;
}

static int index_baseclass(struct kndClass *self, struct kndClass *base, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClassRef *ref, *baseref;
    // struct kndClass *base_copy = NULL;
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *match;
    struct kndClass *c;
    struct kndSet *desc_idx;
    bool parent_linked = false;
    int err;

    /* register as a child */
    err = knd_class_ref_new(&ref, mempool);
    KND_TASK_ERR("failed to alloc class ref");
    ref->entry = entry;
    ref->class = self;
    set_child_ref(base, ref);

    if (task->type == KND_BULK_LOAD_STATE)
        base->num_children++;

    /* update ancestors' indices */
    FOREACH (baseref, base->ancestors) {

        err = knd_class_acquire(baseref->entry, &c, task);
        KND_TASK_ERR("failed to acquire {class %.*s}",
                     baseref->entry->name_size, baseref->entry->name);

        if (c->state_top) continue;

        err = index_ancestor(self, c, task);
        KND_TASK_ERR("failed to index ancestor {class %.*s} of {class %.*s}",
                     c->name_size, c->name, base->name_size, base->name);
    }

    if (DEBUG_CLASS_INDEX_LEVEL_2) {
        knd_log(".. add {class %.*s} as a child of {class %.*s}",
                self->name_size, self->name, base->name_size, base->name);
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

    knd_log(".. update {repo %.*s {class %.*s}} indices {idx %p}",
            repo->name_size, repo->name, self->name_size, self->name, idx);

    return knd_OK;
}

int knd_class_index(struct kndClass *self, struct kndTask *task)
{
    struct kndClassBasePred *bp;
    struct kndClass *c;
    struct kndAttrStm *stm;
    int err;

    if (self->indexing_in_progress) {
        knd_log("-- vicious circle detected in {class %.*s} while indexing",
                self->name_size, self->name);
        return knd_FAIL;
    }
    self->indexing_in_progress = true;

    if (DEBUG_CLASS_INDEX_LEVEL_2) {
        knd_log(".. indexing {class %.*s {id %.*s}}",
                self->entry->name_size, self->entry->name,
                self->entry->id_size, self->entry->id);
    }

    FOREACH (bp, self->base_preds) {
        err = knd_class_acquire(bp->entry, &c, task);
        KND_TASK_ERR("failed to acquire {class %.*s}", bp->entry->name_size, bp->entry->name);

        if (!c->is_indexed) {
            err = index_baseclass(self, c, task);
            KND_TASK_ERR("failed to index a baseclass");
        }

        FOREACH (stm, bp->attr_stms) {
            if (stm->attr->is_a_set) {
                err = knd_index_attr_stm_list(self->entry, stm->attr, stm, task);
                KND_TASK_ERR("failed to index attr stm list %.*s",
                             stm->attr->name_size, stm->attr->name);
                continue;
            }

            err = knd_index_attr_stm(self->entry, stm->attr, stm, task);
            KND_TASK_ERR("failed to index attr stm %.*s",
                         stm->attr->name_size, stm->attr->name);
        }
    }
    self->is_indexed = true;
    return knd_OK;
}
