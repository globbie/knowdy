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

#define DEBUG_CLASS_RESOLVE_LEVEL_1 0
#define DEBUG_CLASS_RESOLVE_LEVEL_2 0
#define DEBUG_CLASS_RESOLVE_LEVEL_3 0
#define DEBUG_CLASS_RESOLVE_LEVEL_4 0
#define DEBUG_CLASS_RESOLVE_LEVEL_5 0
#define DEBUG_CLASS_RESOLVE_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndClass *class;
    struct kndClass *baseclass;
    struct kndClassBasePred *base_pred;
};

static int resolve_base(struct kndClass *self, struct kndTask *task);

static int inherit_attr(void *obj, const char *unused_var(elem_id), size_t unused_var(elem_id_size),
                        size_t unused_var(count), void *elem)
{
    struct LocalContext *ctx = obj;
    struct kndTask    *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndClass   *self = ctx->class;
    struct kndSet     *attr_idx = self->attr_idx;
    struct kndAttrRef *src_ref = elem;
    struct kndAttr    *attr    = src_ref->attr;
    struct kndAttrRef *ref = NULL;
    int err;

    err = knd_set_get(attr_idx, attr->id, attr->id_size, (void**)&ref);
    if (!err) {
        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
            knd_log("..  \"%.*s\" (id:%.*s) attr already active in \"%.*s\"..",
                    attr->name_size, attr->name, attr->id_size, attr->id,
                    self->name_size, self->name);
        }
        /* override an existing attr var */
        if (ref->attr_stm && src_ref->attr_stm) {
            if (DEBUG_CLASS_RESOLVE_LEVEL_3) {
                knd_log("..  \"%.*s\" (id:%.*s) attr var already set in \"%.*s\" => %.*s",
                        attr->name_size, attr->name, attr->id_size, attr->id,
                        self->name_size, self->name, ref->attr_stm->val_size, ref->attr_stm->val);
                knd_log("override with new val: %.*s",
                            src_ref->attr_stm->val_size, src_ref->attr_stm->val);
            }
            ref->attr_stm = src_ref->attr_stm;
            ref->class_entry = src_ref->class_entry;
            return knd_OK;
        }
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) 
        knd_log("..  \"%.*s\" (id:%.*s attr_stm:%p) attr inherited by %.*s..",
                attr->name_size, attr->name, attr->id_size, attr->id, src_ref->attr_stm,
                self->name_size, self->name);

    if (ref) {
        if (src_ref->attr_stm) {
            ref->attr_stm = src_ref->attr_stm;
            ref->class_entry = src_ref->class_entry;
        }
        return knd_OK;
    }

    err = knd_attr_ref_new(&ref, mempool);
    KND_TASK_ERR("failed to alloc an attr ref err:%d", err);

    ref->attr = attr;
    ref->attr_stm = src_ref->attr_stm;
    ref->class_entry = src_ref->class_entry;

    err = knd_set_add(attr_idx, attr->id, attr->id_size, (void*)ref);
    KND_TASK_ERR("failed to update attr idx of %.*s", self->name_size, self->name);

    // inherit implied attr
    if (attr->is_implied && !self->implied_attr)
        self->implied_attr = attr;

    return knd_OK;
}

static int inherit_attrs(struct kndClass *self, struct kndClass *base, struct kndTask *task)
{
    int err;

    if (base->phase < KND_CLASS_RESOLVED) {
        err = knd_class_resolve(base, task);
        KND_TASK_ERR("base {class %.*s} failed to resolve", base->name_size, base->name);
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. \"%.*s\" class to inherit attrs from \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                base->name_size, base->name);
    }
    struct LocalContext ctx = {
        .task = task,
        .class = self,
        .baseclass = base
    };
    err = base->attr_idx->map(base->attr_idx, inherit_attr, (void*)&ctx);
    KND_TASK_ERR("class \"%.*s\" failed to inherit attrs from \"%.*s\"",
                 self->name_size, self->name, base->name_size, base->name);
    return knd_OK;
}

static int link_ancestor(struct kndClass *self, struct kndClass *baseclass, struct kndTask *task)
{
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *prev_entry;
    struct kndMemPool *mempool = task->mempool;
    struct kndClassRef *ref;
    struct kndClass *c;
    struct kndDict *class_name_idx = task->class_name_idx;
    int err;

    /* check doublets */
    FOREACH (ref, self->ancestors) {
        err = knd_class_acquire(ref->entry, &c, task);
        KND_TASK_ERR("failed to acquire {class %.*s}", ref->entry->name_size, ref->entry->name);

        if (c == baseclass) return knd_OK;
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. %.*s class to link an ancestor {class %.*s} {top %d}",
                self->name_size, self->name,
                baseclass->name_size, baseclass->name, baseclass->state_top);

    if (baseclass->entry->repo != entry->repo) {
        prev_entry = knd_dict_get(class_name_idx, baseclass->name, baseclass->name_size);
        if (prev_entry) {
            err = knd_class_acquire(prev_entry, &c, task);
            KND_TASK_ERR("failed to acquire {class %.*s}", prev_entry->name_size, prev_entry->name);
            baseclass = c;
        } else {
            knd_log("-- {class %.*s} not found in {repo %.*s}",
                    baseclass->name_size, baseclass->name,
                    self->entry->repo->name_size, self->entry->repo->name);

            // err = knd_class_clone(base_entry->class,
            //                      self->entry->repo, &base, task);             RET_ERR();
        }
    }

    /* add an ancestor */
    err = knd_class_ref_new(&ref, mempool);
    RET_ERR();
    ref->class = baseclass;
    ref->entry = baseclass->entry;
    ref->next = self->ancestors;
    self->ancestors = ref;
    self->num_ancestors++;
    return knd_OK;
}

static int link_baseclass(struct kndClass *self, struct kndClass *base, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClassRef *ref, *baseref;
    struct kndClassEntry *entry = self->entry;
    struct kndClass *c;
    // struct kndRepo *repo = self->entry->repo;
    bool parent_linked = false;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. \"%.*s\" (%.*s) links to base => \"%.*s\" (%.*s)",
                entry->name_size, entry->name, entry->repo->name_size, entry->repo->name,
                base->entry->name_size, base->entry->name,
                base->entry->repo->name_size, base->entry->repo->name);

    /* if (base->entry->repo != repo) {
        err = knd_class_clone(base, repo, &base_copy, task);                   RET_ERR();
        base = base_copy;
        err = link_ancestor(self, base->entry, task);                             RET_ERR();
        parent_linked = true;
        } */

    /* copy the ancestors */
    FOREACH (baseref, base->ancestors) {
        err = knd_class_acquire(baseref->entry, &c, task);
        KND_TASK_ERR("failed to acquire {class %.*s}",
                     baseref->entry->name_size, baseref->entry->name);

        if (c->state_top) continue;

        err = link_ancestor(self, c, task);
        RET_ERR();
    }

    if (!parent_linked) {
        /* register a parent */
        err = knd_class_ref_new(&ref, mempool);
        KND_TASK_ERR("mempool failed to alloc a class ref");
        ref->class = base;
        ref->entry = base->entry;
        ref->next = self->ancestors;
        self->ancestors = ref;
        self->num_ancestors++;
    }
    return knd_OK;
}

static int resolve_baseclasses(struct kndClass *self, struct kndTask *task)
{
    struct kndClassBasePred *bp;
    struct kndClass *c = NULL;
    struct kndRepo *repo = task->repo;
    const char *classname;
    size_t classname_size;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. {class %.*s to resolve its bases", self->name_size, self->name);
    }

    if (self->phase >= KND_CLASS_BASE_RESOLVED) {
        knd_log("-- vicious circle detected in resolving bases of {class %.*s}",
                self->name_size, self->name);
        return knd_FAIL;
    }

    FOREACH (bp, self->base_preds) {
        classname = bp->name;
        classname_size = bp->name_size;
        if (!classname_size) {
            err = knd_FAIL;
            KND_TASK_ERR("no base class name specified in {class %.*s}",
                         self->name_size, self->name);
        }
        err = knd_get_class(repo, classname, classname_size, &c, task);
        KND_TASK_ERR("no {class %.*s} found in {repo %.*s}",
                     classname_size, classname, repo->name_size, repo->name);

        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
            knd_log("++ \"%.*s\" ref established as a base class for \"%.*s\"!",
                    bp->entry->name_size, bp->entry->name,
                    self->entry->name_size, self->entry->name);
        }

        if (c->phase < KND_CLASS_BASE_RESOLVED) {
            err = resolve_base(c, task);
            RET_ERR();
        }

        err = link_baseclass(self, c, task);
        RET_ERR();

        bp->entry = c->entry;
    }

    self->phase = KND_CLASS_BASE_RESOLVED;
    return knd_OK;
}

int knd_class_resolve(struct kndClass *self, struct kndTask *task)
{
    struct kndClassBasePred *bp;
    struct kndClassEntry *entry = self->entry;
    struct kndClass *c;
    struct kndAttrRef *attr_ref, *ref;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. resolving {class %.*s} {num-attrs %zu} {phase %d}",
                entry->name_size, entry->name, self->num_attrs, self->phase);
    }

    if (self->phase >= KND_CLASS_RESOLVED) {
        knd_log("-- vicious circle detected in resolving {class %.*s}", self->name_size, self->name);
        return knd_FAIL;
    }

    /* primary attrs */
    if (self->num_attrs) {
        err = knd_resolve_primary_attrs(self, task);
        KND_TASK_ERR("failed to resolve primary attrs of {class %.*s}",
                     entry->name_size, entry->name);
    }

    if (self->phase < KND_CLASS_BASE_RESOLVED) {
        err = resolve_baseclasses(self, task);
        KND_TASK_ERR("failed to resolve base classes of {class %.*s}",
                     self->name_size, self->name);
    }

    FOREACH (bp, self->base_preds) {
        err = knd_class_acquire(bp->entry, &c, task);
        KND_TASK_ERR("failed to acquire class %.*s",
                     bp->entry->name_size, bp->entry->name);


        err = inherit_attrs(self, c, task);
        KND_TASK_ERR("failed to inherit attrs from {class %.*s}", c->name_size, c->name);

        if (bp->num_attr_stms) {

            if (DEBUG_CLASS_RESOLVE_LEVEL_3) {
                knd_log(".. {cls %.*s} to resolve {base %.*s {num-attr-stms %zu}",
                        self->name_size, self->name, c->name_size, c->name,
                        bp->num_attr_stms);
            }

            err = knd_resolve_attr_stms(self, bp, task);
            KND_TASK_ERR("failed to resolve attr stms from {class %.*s}", c->name_size, c->name);
        }
    }

    /* uniq attr constraints */
    FOREACH (ref, self->uniq) {
        err = knd_class_get_attr(self, ref->name, ref->name_size, &attr_ref);
        KND_TASK_ERR("no uniq {attr %.*s} in {class %.*s}",
                     ref->name_size, ref->name, self->name_size, self->name);
        ref->attr = attr_ref->attr;
    }

    self->phase = KND_CLASS_RESOLVED;

    /* this class is good to go: 
       assign a unique class id */
    // TODO: check Writer Role
    entry->numid = atomic_fetch_add_explicit(&task->idxs->class_id_count, 1, memory_order_relaxed);
    entry->numid++;
    knd_uid_create(entry->numid, entry->id, &entry->id_size);

    if (DEBUG_CLASS_RESOLVE_LEVEL_3) {
        knd_log("++ {class %.*s} resolved!",
                entry->name_size, entry->name);
    }

    return knd_OK;
}

static int resolve_base(struct kndClass *self, struct kndTask *task)
{
    struct kndClassEntry *entry = self->entry;
    int err;

    if (self->phase >= KND_CLASS_BASE_RESOLVED) {
        err = knd_FAIL;
        KND_TASK_ERR("vicious circle detected while resolving the bases of {class %.*s}",
                     entry->name_size, entry->name);
    }

    err = resolve_baseclasses(self, task);
    KND_TASK_ERR("failed to resolve baseclasses of %.*s", entry->name_size, entry->name);

    return knd_OK;
}

int knd_resolve_class_ref(struct kndRepo *repo, const char *name, size_t name_size,
                          struct kndClass *base, struct kndClass **result,
                          struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndClass *c;
    struct kndSharedDict *class_name_idx = task->idxs->class_name_idx;
    int err;

    assert (name_size != 0 && name != NULL);

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. checking {class-ref %.*s}..", name_size, name);
        if (base) {
            knd_log(".. {base-template %.*s}..", base->name_size, base->name);
        }
    }

    /* initial bulk load */
    if (task->type == KND_BULK_LOAD_STATE) {
        entry = knd_shared_dict_get(class_name_idx, name, name_size);
        if (!entry) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("failed to resolve a class ref to \"%.*s\"", name_size, name);
        }

        c = entry->cached_version;
        if (!c) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("no cached version of {class %.*s}", name_size, name);
        }

        if (c->phase < KND_CLASS_BASE_RESOLVED) {
            err = resolve_base(c, task);
            KND_TASK_ERR("failed to resolve base classes of %.*s", name_size, name);
        }

        if (base) {
            if (base->phase < KND_CLASS_BASE_RESOLVED) {
                err = resolve_base(base, task);
                KND_TASK_ERR("failed to resolve base classes of %.*s", name_size, name);
            }
            if (base != c) {
                err = knd_is_base(base, c);
                KND_TASK_ERR("no inheritance from %.*s to %.*s",
                             base->name_size, base->name, c->name_size, c->name);
            }
        }
        *result = c;
        return knd_OK;
    }

    err = knd_get_class(repo, name, name_size, &c, task);
    KND_TASK_ERR("{class %.*s} not found in {repo %.*s}", name_size, name,
                 repo->name_size, repo->name);

    if (c->phase < KND_CLASS_BASE_RESOLVED) {
        err = resolve_base(c, task);
        RET_ERR();
    }

    if (base) {
        if (base->phase < KND_CLASS_BASE_RESOLVED) {
            err = resolve_base(base, task);
            KND_TASK_ERR("failed to resolve class %.*s", base->name_size, base->name);
        }
        err = knd_is_base(base, c);
        KND_TASK_ERR("no inheritance from %.*s to %.*s",
                     base->name_size, base->name, c->name_size, c->name);
    }
    *result = c;
    return knd_OK;
}

