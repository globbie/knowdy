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
    struct kndClassVar *class_var;
};

static int index_attr(void *obj, const char *unused_var(elem_id), size_t unused_var(elem_id_size),
                      size_t unused_var(count), void *elem)
{
    struct LocalContext *ctx = obj;
    struct kndTask    *task = ctx->task;
    struct kndClass   *self = ctx->class;
    struct kndAttrRef *src_ref  = elem;
    struct kndAttr    *attr     = src_ref->attr;
    struct kndAttrVar *var     = src_ref->attr_var;
    int err;

    if (!var) {
        // TODO: reverse links from class templates to attrs
        //err = knd_attr_index(self, attr, task);
        //KND_TASK_ERR("failed to index attr %.*s", attr->name_size, attr->name);
        return knd_OK;
    }
    if (!attr->is_indexed) return knd_OK;

    /* NB: only directly owned attr vars are indexed */
    if (var->class_var->parent != self) return knd_OK;

    if (attr->is_a_set) {
        err = knd_index_attr_var_list(self->entry, attr, var, task);
        KND_TASK_ERR("failed to index attr var list %.*s", attr->name_size, attr->name);
        return knd_OK;
    }
    err = knd_index_attr_var(self->entry, attr, var, task);
    KND_TASK_ERR("failed to index attr var %.*s", attr->name_size, attr->name);
    return knd_OK;
}

static int index_ancestor(struct kndClass *self, struct kndClassEntry *base_entry,
                          struct kndTask *task)
{
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *prev_entry;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *desc_idx;
    struct kndClass *base;
    struct kndDict *class_name_idx = task->class_name_idx;
    void *result;
    int err;

    base = base_entry->class;

    if (DEBUG_CLASS_INDEX_LEVEL_2)
        knd_log(".. %.*s class to update desc_idx of an ancestor: \"%.*s\" top:%d",
                self->name_size, self->name,
                base->name_size, base->name, base->state_top);

    if (base_entry->repo != entry->repo) {
        prev_entry = knd_dict_get(class_name_idx,
                                  base_entry->name, base_entry->name_size);
        if (prev_entry) {
            base = prev_entry->class;
        } else {
            knd_log("-- class \"%.*s\" not found in repo \"%.*s\"",
                    base_entry->name_size, base_entry->name,
                    self->entry->repo->name_size, self->entry->repo->name);

            //err = knd_class_clone(base_entry->class,
            //                      self->entry->repo, &base, task);             RET_ERR();
        }
    }

    desc_idx = base->descendants;
    if (!desc_idx) {
        err = knd_set_new(mempool, &desc_idx);
        KND_TASK_ERR("failed to alloc a set");
        desc_idx->type = KND_SET_CLASS;
        desc_idx->base = base->entry;
        base->descendants = desc_idx;
    }

    err = desc_idx->get(desc_idx, entry->id, entry->id_size, &result);
    if (!err) {
        if (DEBUG_CLASS_INDEX_LEVEL_2)
            knd_log("== index already present between %.*s (%.*s)"
                    " and its ancestor %.*s",
                    entry->name_size, entry->name,
                    entry->id_size, entry->id,
                    base->entry->name_size,
                    base->entry->name);
        return knd_OK;
    }
    base->num_terminals++;

    /* register as a descendant */
    err = desc_idx->add(desc_idx, entry->id, entry->id_size, (void*)entry);
    KND_TASK_ERR("failed to register a descendant");

    return knd_OK;
}

static inline void set_child_ref(struct kndClassEntry *self, struct kndClassRef *child_ref)
{
    child_ref->next = self->class->children;
    // TODO atomic
    self->class->children = child_ref;
    self->class->num_children++;
}

static int index_baseclass(struct kndClass *self, struct kndClass *base, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClassRef *ref, *baseref;
    // struct kndClass *base_copy = NULL;
    struct kndClassEntry *entry = self->entry;
    // struct kndRepo *repo = self->entry->repo;
    struct kndSet *desc_idx;
    bool parent_linked = false;
    int err;

    if (DEBUG_CLASS_INDEX_LEVEL_2)
        knd_log(".. \"%.*s\" (%.*s) links to base => \"%.*s\" (%.*s) top:%d",
                entry->name_size, entry->name,
                entry->repo->name_size, entry->repo->name,
                base->entry->name_size, base->entry->name,
                base->entry->repo->name_size, base->entry->repo->name,
                base->entry->class->state_top);

    /*if (base->entry->repo != repo) {
        err = knd_class_clone(base, repo, &base_copy, task);                   RET_ERR();
        base = base_copy;
        err = index_ancestor(self, base->entry, task);                             RET_ERR();
        parent_linked = true;
        }*/

    /* register as a child */
    err = knd_class_ref_new(mempool, &ref);
    KND_TASK_ERR("failed to alloc class ref");
    ref->entry = entry;
    ref->class = self;
    set_child_ref(base->entry, ref);

    if (task->type == KND_BULK_LOAD_STATE)
        base->num_children++;

    /* update ancestors' indices */
    FOREACH (baseref, base->ancestors) {
        if (baseref->entry->class && baseref->entry->class->state_top)
            continue;
        err = index_ancestor(self, baseref->entry, task);
        RET_ERR();
    }

    if (!parent_linked) {
        base->num_terminals++;

        if (DEBUG_CLASS_INDEX_LEVEL_2)
            knd_log(".. add \"%.*s\" (repo:%.*s) as a child of \"%.*s\" (repo:%.*s)..",
                    entry->name_size, entry->name,
                    entry->repo->name_size, entry->repo->name,
                    base->entry->name_size, base->entry->name,
                    base->entry->repo->name_size, base->entry->repo->name);

        /* register a descendant */
        desc_idx = base->descendants;
        if (!desc_idx) {
            err = knd_set_new(mempool, &desc_idx);
            KND_TASK_ERR("failed to alloc a desc idx set");
            desc_idx->type = KND_SET_CLASS;
            desc_idx->base = base->entry;
            base->descendants = desc_idx;
        }
        err = desc_idx->add(desc_idx, entry->id, entry->id_size,
                            (void*)entry);
        RET_ERR();
    }
    return knd_OK;
}

static int index_baseclasses(struct kndClass *self, struct kndTask *task)
{
    struct kndClassVar *cvar;
    int err;

    if (DEBUG_CLASS_INDEX_LEVEL_2)
        knd_log(".. class \"%.*s\" to update its base class indices..",
                self->name_size, self->name);

    FOREACH (cvar, self->baseclass_vars) {
        err = index_baseclass(self, cvar->entry->class, task);
        KND_TASK_ERR("failed to index a baseclass");
    }
    return knd_OK;
}

int knd_class_update_indices(struct kndRepo *repo, struct kndClassEntry *self,
                             struct kndState *unused_var(state),
                             struct kndTask *unused_var(task))
{
    struct kndSharedSet *idx = repo->class_idx;
    //struct kndStateRef *ref;
    //int err;

    knd_log(".. update class %.*s indices: idx:%p", self->name_size, self->name, idx);
    
    return knd_OK;
}

int knd_class_index(struct kndClass *self, struct kndTask *task)
{
    struct kndRepo *repo = self->entry->repo;
    int err;

    if (self->is_indexed) return knd_OK;

    if (self->indexing_in_progress) {
        knd_log("-- vicious circle detected in \"%.*s\" while indexing",
                self->name_size, self->name);
        return knd_FAIL;
    }
    self->indexing_in_progress = true;

    if (DEBUG_CLASS_INDEX_LEVEL_2) {
        knd_log(".. indexing {class %.*s {id %.*s}}",
                self->entry->name_size, self->entry->name,
                self->entry->id_size, self->entry->id);
    }
    /* a child of the root class */
    if (!self->baseclass_vars) {
        err = index_baseclass(self, repo->root_class, task);
        KND_TASK_ERR("failed to index class of a root");
    } else {
        err = index_baseclasses(self, task);
        KND_TASK_ERR("failed to index baseclasses");
    }

    struct LocalContext ctx = {
        .task = task,
        .class = self
    };
    err = knd_set_map(self->attr_idx, index_attr, (void*)&ctx);
    KND_TASK_ERR("failed to index attrs of class %.*s", self->name_size, self->name);

    if (DEBUG_CLASS_INDEX_LEVEL_2)
        knd_log("++ {class %.*s {id %.*s}} indexed!",
                self->entry->name_size, self->entry->name,
                self->entry->id_size, self->entry->id);

    self->is_indexed = true;
    return knd_OK;
}
