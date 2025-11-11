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

static int inherit_attr(void *elem, void *ctx_obj)
{
    struct kndAttrRef *src_ref = elem;
    struct LocalContext *ctx = ctx_obj;
    struct kndTask    *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndClass   *self = ctx->class;
    struct kndSet     *attr_idx = self->attr_idx;
    struct kndAttr    *attr    = src_ref->attr;
    struct kndAttrRef *ref = NULL;
    int err;

    err = knd_set_get(attr_idx, attr->id, attr->id_size, (void**)&ref);
    if (!err) {
        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
            knd_log("..  {attr %.*s {id %.*s}} already active in {cls %.*s}..",
                    attr->name_size, attr->name, attr->id_size, attr->id,
                    self->name_size, self->name);
        }

        /* override an existing attr stm */
        if (ref->attr_stm && src_ref->attr_stm) {
            if (DEBUG_CLASS_RESOLVE_LEVEL_3) {
                knd_log("..  {stm %.*s {id %.*s}} already set in \"%.*s\" => %.*s",
                        attr->name_size, attr->name, attr->id_size, attr->id,
                        self->name_size, self->name, ref->attr_stm->val_size, ref->attr_stm->val);
                knd_log("override with new val: %.*s",
                            src_ref->attr_stm->val_size, src_ref->attr_stm->val);
            }
            ref->attr_stm = src_ref->attr_stm;
            ref->cls_entry = src_ref->cls_entry;
            return knd_OK;
        }
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log("..  {attr %.*s {id %.*s}} inherited by {cls %.*s}",
                attr->name_size, attr->name, attr->id_size, attr->id,
                self->name_size, self->name);
    }
    if (ref) {
        if (src_ref->attr_stm) {
            ref->attr_stm = src_ref->attr_stm;
            ref->cls_entry = src_ref->cls_entry;
        }
        return knd_OK;
    }

    err = knd_attr_ref_new(&ref, mempool);
    KND_TASK_ERR("failed to alloc an attr ref err:%d", err);

    ref->attr = attr;
    ref->attr_stm = src_ref->attr_stm;
    ref->cls_entry = src_ref->cls_entry;

    err = knd_set_add(attr_idx, attr->id, attr->id_size, (void*)ref, task);
    KND_TASK_ERR("failed to update attr idx of %.*s", self->name_size, self->name);

    return knd_OK;
}

static int inherit_attrs(struct kndClass *self, struct kndClass *base, struct kndTask *task)
{
    int err;

    if (base->phase < KND_CLASS_RESOLVED) {
        err = knd_class_resolve(base, task);
        KND_TASK_ERR("base {cls %.*s} failed to resolve", base->name_size, base->name);
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. {cls %.*s} to inherit attrs from {cls %.*s}..",
                self->entry->name_size, self->entry->name,
                base->name_size, base->name);
    }
    struct LocalContext ctx = {
        .task = task,
        .class = self,
        .baseclass = base
    };

    err = knd_set_map(base->attr_idx, NULL, NULL, NULL,
                      inherit_attr, (void*)&ctx);
    KND_TASK_ERR("{cls %.*s} failed to inherit attrs from {cls %.*s}",
                 self->name_size, self->name, base->name_size, base->name);
    return knd_OK;
}

static int link_ancestor(struct kndClass *self, struct kndClass *baseclass, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClassRef *ref;
    struct kndClass *c;
    int err;

    /* check doublets */
    FOREACH (ref, self->ancestors) {
        err = knd_class_acquire(ref->entry, &c, task);
        KND_TASK_ERR("failed to acquire {cls %.*s}", ref->entry->name_size, ref->entry->name);

        if (c == baseclass) return knd_OK;
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. %.*s class to link an ancestor {cls %.*s} {top %d}",
                self->name_size, self->name,
                baseclass->name_size, baseclass->name, baseclass->state_top);

    /* add an ancestor */
    err = knd_class_ref_new(&ref, mempool);
    KND_TASK_ERR("failed to alloc a cls ref");
    ref->entry = baseclass->entry;
    ref->next = self->ancestors;
    self->ancestors = ref;
    self->num_ancestors++;
    return knd_OK;
}

static int set_child_ref(struct kndClass *base, struct kndClass *cls, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClassRef *ref;
    int err;

    err = knd_class_ref_new(&ref, mempool);
    KND_TASK_ERR("failed to alloc cls ref");
    ref->entry = cls->entry;
    ref->numid = base->num_children;

    ref->next = base->children;
    base->children = ref;
    base->num_children++;

    if (base->num_children > KND_MAX_FACETS) {
        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
            knd_log("warning: num of subclasses of {cls %.*s} exceeds {max-facet-num %d}",
                    base->name_size, base->name, KND_MAX_FACETS);
        }
    }
    return knd_OK;
}

int knd_class_link_base(struct kndClass *cls, struct kndClass *base, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClassRef *ref, *baseref;
    struct kndClass *c;
    bool parent_linked = false;
    int err;

    err = set_child_ref(base, cls, task);
    KND_TASK_ERR("failed to register child {cls %.*s}", cls->name_size, cls->name);

    /* copy the ancestors */
    FOREACH (baseref, base->ancestors) {
        err = knd_class_acquire(baseref->entry, &c, task);
        KND_TASK_ERR("failed to acquire {cls %.*s}", baseref->entry->name_size, baseref->entry->name);

        if (c->state_top) continue;

        err = link_ancestor(cls, c, task);
        KND_TASK_ERR("failed to link an ancestor");
    }

    if (!parent_linked) {
        /* register a parent */
        err = knd_class_ref_new(&ref, mempool);
        KND_TASK_ERR("mempool failed to alloc a class ref");
        ref->entry = base->entry;
        ref->next = cls->ancestors;
        cls->ancestors = ref;
        cls->num_ancestors++;
    }
    return knd_OK;
}

static int resolve_baseclasses(struct kndClass *cls, struct kndTask *task)
{
    struct kndClassBasePred *bp;
    struct kndClass *c = NULL;
    size_t numid;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. {cls %.*s} to resolve its bases", cls->name_size, cls->name);
    }

    if (cls->phase >= KND_CLASS_BASE_RESOLVED) {
        knd_log("-- vicious circle detected in resolving bases of {cls %.*s}",
                cls->name_size, cls->name);
        return knd_FAIL;
    }

    FOREACH (bp, cls->base_preds) {
        if (!bp->name_size) {
            err = knd_FAIL;
            KND_TASK_ERR("no base class name specified in {cls %.*s}",
                         cls->name_size, cls->name);
        }
        err = knd_get_cls_by_name(bp->name, bp->name_size, &c, task);
        KND_TASK_ERR("no {cls %.*s} found", bp->name_size, bp->name);

        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
            knd_log("++ {cls %.*s} established as a base for {cls %.*s}",
                    bp->entry->name_size, bp->entry->name,
                    cls->entry->name_size, cls->entry->name);
        }

        if (c->phase < KND_CLASS_BASE_RESOLVED) {
            err = resolve_base(c, task);
            KND_TASK_ERR("failed to resolve base {cls %.*s}", c->name_size, c->name);
        }

        /* check if subclass is already registered in the base class */
        err = knd_class_is_direct_child(c, cls, &numid);
        if (err != knd_NO_MATCH) {
            if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
                knd_log("-- child {cls %.*s} already registered in base class?",
                        cls->name_size, cls->name);
            }
            continue;
        }

        err = knd_class_link_base(cls, c, task);
        KND_TASK_ERR("failed to link {cls %.*s} to its base {cls %.*s}",
                     cls->name_size, cls->name, c->name_size, c->name);

        bp->entry = c->entry;
    }

    cls->phase = KND_CLASS_BASE_RESOLVED;
    return knd_OK;
}

int knd_class_resolve(struct kndClass *self, struct kndTask *task)
{
    struct kndClassBasePred *bp;
    struct kndClassEntry *entry = self->entry;
    struct kndClass *c;
    struct kndAttrRef *attr_ref, *ref;
    struct kndAttrStm *stm;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. resolving {cls %.*s {id %.*s}} {num-attrs %zu} {phase %d}",
                entry->name_size, entry->name, entry->id_size, entry->id,
                self->num_attrs, self->phase);
    }

    if (self->phase >= KND_CLASS_RESOLVED) {
        knd_log("-- vicious circle detected in resolving {cls %.*s}", self->name_size, self->name);
        return knd_FAIL;
    }

    /* primary attrs */
    if (self->num_attrs) {
        err = knd_resolve_primary_attrs(self, task);
        KND_TASK_ERR("failed to resolve primary attrs of {cls %.*s}",
                     entry->name_size, entry->name);
    }

    if (self->phase < KND_CLASS_BASE_RESOLVED) {
        err = resolve_baseclasses(self, task);
        KND_TASK_ERR("failed to resolve base classes of {cls %.*s}",
                     self->name_size, self->name);
    }

    FOREACH (bp, self->base_preds) {
        err = knd_class_acquire(bp->entry, &c, task);
        KND_TASK_ERR("failed to acquire {cls %.*s}", bp->entry->name_size, bp->entry->name);

        err = inherit_attrs(self, c, task);
        KND_TASK_ERR("failed to inherit attrs from {cls %.*s}", c->name_size, c->name);

        FOREACH (stm, bp->attr_stms) {
            err = knd_resolve_attr_stm(self, stm, task);
            KND_TASK_ERR("failed to resolve attr stm {cls %.*s {%.*s}}",
                         c->name_size, c->name, stm->name_size, stm->name);
        }
    }

    /* uniq attr constraints */
    FOREACH (ref, self->uniq) {
        err = knd_class_get_attr(self, ref->name, ref->name_size, &attr_ref);
        KND_TASK_ERR("no uniq {attr %.*s} in {cls %.*s}",
                     ref->name_size, ref->name, self->name_size, self->name);
        ref->attr = attr_ref->attr;
    }

    self->phase = KND_CLASS_RESOLVED;

    if (DEBUG_CLASS_RESOLVE_LEVEL_3) {
        knd_log("++ {cls %.*s {id %.*s}} resolved!",
                entry->name_size, entry->name, entry->id_size, entry->id);
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
    KND_TASK_ERR("failed to resolve baseclasses of {cls %.*s}", entry->name_size, entry->name);

    return knd_OK;
}

int knd_resolve_cls_ref(const char *name, size_t name_size,
                        struct kndClass *base, struct kndClass **result,
                        struct kndTask *task)
{
    struct kndClass *c;
    int err;

    assert (name_size != 0 && name != NULL);

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. checking {cls-ref %.*s}..", name_size, name);
        if (base) {
            knd_log(".. {base-template %.*s}..", base->name_size, base->name);
        }
    }

    /* initial bulk load */
    if (task->type == KND_TASK_BULK_LOAD) {
        err = knd_get_cls_by_name(name, name_size, &c, task);
        KND_TASK_ERR("failed to resolve a ref to {cls %.*s}", name_size, name);

        if (c->phase < KND_CLASS_BASE_RESOLVED) {
            err = resolve_base(c, task);
            KND_TASK_ERR("failed to resolve bases of {cls %.*s}", name_size, name);
        }

        if (base) {
            if (base->phase < KND_CLASS_BASE_RESOLVED) {
                err = resolve_base(base, task);
                KND_TASK_ERR("failed to resolve base classes of %.*s", name_size, name);
            }
            if (base != c) {
                err = knd_class_is_base(base, c);
                KND_TASK_ERR("no inheritance from {cls %.*s} to {cls %.*s}",
                             base->name_size, base->name, c->name_size, c->name);
            }
        }
        *result = c;
        return knd_OK;
    }

    err = knd_get_cls_by_name(name, name_size, &c, task);
    KND_TASK_ERR("{cls %.*s} not found", name_size, name);

    if (c->phase < KND_CLASS_BASE_RESOLVED) {
        err = resolve_base(c, task);
        KND_TASK_ERR("failed to resolve {cls %.*s}", c->name_size, c->name);
    }

    if (base) {
        if (base->phase < KND_CLASS_BASE_RESOLVED) {
            err = resolve_base(base, task);
            KND_TASK_ERR("failed to resolve {cls %.*s}", base->name_size, base->name);
        }
        err = knd_class_is_base(base, c);
        KND_TASK_ERR("no inheritance from {cls %.*s} to {cls %.*s}",
                     base->name_size, base->name, c->name_size, c->name);
    }

    *result = c;
    return knd_OK;
}

