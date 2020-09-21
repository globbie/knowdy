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
    struct kndClassVar *class_var;
};

static int resolve_base(struct kndClass *self, struct kndTask *task);

static int inherit_attr(void *obj,
                        const char *unused_var(elem_id),
                        size_t unused_var(elem_id_size),
                        size_t unused_var(count),
                        void *elem)
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

    err = attr_idx->get(attr_idx, attr->id, attr->id_size, (void**)&ref);
    if (!err) {
        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
            knd_log("..  \"%.*s\" (id:%.*s) attr already active in \"%.*s\"..",
                    attr->name_size, attr->name,
                    attr->id_size, attr->id,
                    self->name_size, self->name);
        }
        /* no need to override an existing attr var */
        if (ref->attr_var) return knd_OK;
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) 
        knd_log("..  \"%.*s\" (id:%.*s attr_var:%p) attr inherited by %.*s..",
                attr->name_size, attr->name,
                attr->id_size, attr->id,
                src_ref->attr_var,
                self->name_size, self->name);

    if (ref) {
        if (src_ref->attr_var) {
            ref->attr_var = src_ref->attr_var;
            ref->class_entry = src_ref->class_entry;
        }
        return knd_OK;
    }

    /* new attr entry */
    err = knd_attr_ref_new(mempool, &ref);
    KND_TASK_ERR("failed to alloc an attr ref");
    ref->attr = attr;
    ref->attr_var = src_ref->attr_var;
    ref->class_entry = src_ref->class_entry;

    err = attr_idx->add(attr_idx, attr->id, attr->id_size, (void*)ref);
    KND_TASK_ERR("failed to update attr idx of %.*s", self->name_size, self->name);
    return knd_OK;
}

static int inherit_attrs(struct kndClass *self, struct kndClass *base, struct kndTask *task)
{
    int err;

    if (!base->is_resolved) {
        err = knd_class_resolve(base, task);
        KND_TASK_ERR("base class \"%.*s\" failed to resolve", base->name_size, base->name);
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

static int link_ancestor(struct kndClass *self, struct kndClassEntry *base_entry, struct kndTask *task)
{
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *prev_entry;
    struct kndMemPool *mempool = task->mempool;
    struct kndClass *base;
    struct kndClassRef *ref;
    struct kndDict *class_name_idx = task->class_name_idx;
    int err;

    base = base_entry->class;

    /* check doublets */
    for (ref = self->ancestors; ref; ref = ref->next) {
        if (ref->class == base) return knd_OK;
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. %.*s class to link an ancestor: \"%.*s\" top:%d",
                self->name_size, self->name,
                base->name_size, base->name, base->state_top);

    if (base_entry->repo != entry->repo) {
        prev_entry = knd_dict_get(class_name_idx,
                                  base_entry->name,
                                  base_entry->name_size);
        if (prev_entry) {
            base = prev_entry->class;
        } else {
            knd_log("-- class \"%.*s\" not found in repo \"%.*s\"",
                    base_entry->name_size, base_entry->name,
                    self->entry->repo->name_size, self->entry->repo->name);

            err = knd_class_clone(base_entry->class,
                                  self->entry->repo, &base, task);             RET_ERR();
        }
    }

    /* add an ancestor */
    err = knd_class_ref_new(mempool, &ref);                                       RET_ERR();
    ref->class = base;
    ref->entry = base->entry;
    ref->next = self->ancestors;
    self->ancestors = ref;
    self->num_ancestors++;
    return knd_OK;
}

static int link_baseclass(struct kndClass *self, struct kndClass *base, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClassRef *ref, *baseref;
    struct kndClass *base_copy = NULL;
    struct kndClassEntry *entry = self->entry;
    struct kndRepo *repo = self->entry->repo;
    bool parent_linked = false;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. \"%.*s\" (%.*s) links to base => \"%.*s\" (%.*s) top:%d",
                entry->name_size, entry->name, entry->repo->name_size, entry->repo->name,
                base->entry->name_size, base->entry->name,
                base->entry->repo->name_size, base->entry->repo->name,
                base->entry->class->state_top);

    if (base->entry->repo != repo) {
        err = knd_class_clone(base, repo, &base_copy, task);                   RET_ERR();
        base = base_copy;
        err = link_ancestor(self, base->entry, task);                             RET_ERR();
        parent_linked = true;
    }

    /* copy the ancestors */
    for (baseref = base->ancestors; baseref; baseref = baseref->next) {
        if (baseref->entry->class && baseref->entry->class->state_top) continue;
        err = link_ancestor(self, baseref->entry, task);                          RET_ERR();
    }

    if (!parent_linked) {
        /* register a parent */
        err = knd_class_ref_new(mempool, &ref);
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
    struct kndClassVar *cvar;
    struct kndClassEntry *entry;
    struct kndClass *c = NULL;
    struct kndRepo *repo = self->entry->repo;
    const char *classname;
    size_t classname_size;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_1)
        knd_log(".. class \"%.*s\" to resolve its bases", self->name_size, self->name);

    for (cvar = self->baseclass_vars; cvar; cvar = cvar->next) {
        if (cvar->entry->class == self) {
            /* TODO */
            if (DEBUG_CLASS_RESOLVE_LEVEL_2)
                knd_log(".. \"%.*s\" class to check the update request: \"%s\"..",
                        self->entry->name_size, self->entry->name,
                        cvar->entry->name_size, cvar->entry->name);
            continue;
        }
        c = NULL;
        if (cvar->id_size) {
            err = knd_get_class_by_id(self->entry->repo,
                                      cvar->id, cvar->id_size, &c, task);
            if (err) {
                knd_log("-- no class with id %.*s", cvar->id_size, cvar->id);
                return knd_FAIL;
            }
            entry = c->entry;
            cvar->entry = entry;
        }
        if (!c) {
            classname = cvar->entry->name;
            classname_size = cvar->entry->name_size;
            if (!classname_size) {
                err = knd_FAIL;
                KND_TASK_ERR("no base class specified in class cvar \"%.*s\"",
                             self->entry->name_size, self->entry->name);
            }
            err = knd_get_class(self->entry->repo, classname, classname_size, &c, task);
            KND_TASK_ERR("no class \"%.*s\" found in repo \"%.*s\"",
                         classname_size, classname, repo->name_size, repo->name);
        }

        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            c->str(c, 1);

        if (c == self) {
            knd_log("-- self reference detected in \"%.*s\"",
                    cvar->entry->name_size, cvar->entry->name);
            return knd_FAIL;
        }

        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
            knd_log("++ \"%.*s\" ref established as a base class for \"%.*s\"!",
                    cvar->entry->name_size, cvar->entry->name,
                    self->entry->name_size, self->entry->name);
        }

        if (!c->base_is_resolved) {
            err = resolve_base(c, task);                                RET_ERR();
        }

        err = link_baseclass(self, c, task);                                      RET_ERR();
        cvar->entry->class = c;
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_1) {
        knd_log("++ \"%.*s\" has resolved its baseclasses!",
                self->name_size, self->name);
        self->str(self, 1);
    }

    self->base_is_resolved = true;

    return knd_OK;
}

int knd_class_resolve(struct kndClass *self, struct kndTask *task)
{
    struct kndClassVar *cvar;
    struct kndClassEntry *entry = self->entry;
    struct kndRepo *repo = entry->repo;
    struct kndAttrRef *attr_ref, *ref;
    int err;
    assert(!self->is_resolved);

    if (self->resolving_in_progress) {
        knd_log("-- vicious circle detected in \"%.*s\"", entry->name_size, entry->name);
        return knd_FAIL;
    }
    self->resolving_in_progress = true;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. resolving class \"%.*s\"", entry->name_size, entry->name);

    /* primary attrs */
    if (self->num_attrs) {
        err = knd_resolve_primary_attrs(self, task);
        KND_TASK_ERR("failed to resolve primary attrs of %.*s", entry->name_size, entry->name);
    }

    /* a child of the root class */
    if (!self->baseclass_vars) {
        err = link_baseclass(self, repo->root_class, task);                       RET_ERR();
    } else {
        if (!self->base_is_resolved) {
            err = resolve_baseclasses(self, task);                                RET_ERR();
        }
        for (cvar = self->baseclass_vars; cvar; cvar = cvar->next) {
            err = inherit_attrs(self, cvar->entry->class, task);                  RET_ERR();
            
            if (cvar->attrs) {
                err = knd_resolve_attr_vars(self, cvar, task);                    RET_ERR();
            }
        }
    }

    /* uniq attr constraints */
    FOREACH (ref, self->uniq) {
        err = knd_class_get_attr(self, ref->name, ref->name_size, &attr_ref);
        KND_TASK_ERR("no uniq attr \"%.*s\" in class \"%.*s\"",
                     ref->name_size, ref->name, self->name_size, self->name);
        ref->attr = attr_ref->attr;
    }
    self->is_resolved = true;

    /* this class is good to go: 
       it can now receive a unique class id */
    // TODO: check Writer Role
    entry->numid = atomic_fetch_add_explicit(&repo->class_id_count, 1, memory_order_relaxed);
    entry->numid++;
    knd_uid_create(entry->numid, entry->id, &entry->id_size);

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        entry->class->str(entry->class, 1);

    return knd_OK;
}

static int resolve_base(struct kndClass *self, struct kndTask *task)
{
    struct kndClassEntry *entry = self->entry;
    int err;

    assert(!self->base_is_resolved);

    if (self->base_resolving_in_progress) {
        err = knd_FAIL;
        KND_TASK_ERR("vicious circle detected while resolving the base classes of \"%.*s\"",
                     entry->name_size, entry->name);
    }
    self->base_resolving_in_progress = true;

    err = resolve_baseclasses(self, task);
    KND_TASK_ERR("failed to resolve baseclasses of %.*s", entry->name_size, entry->name);

    self->base_is_resolved = true;
    return knd_OK;
}

int knd_resolve_class_ref(struct kndClass *self, const char *name, size_t name_size, struct kndClass *base,
                          struct kndClass **result, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndClass *c;
    struct kndSharedDict *class_name_idx = self->entry->repo->class_name_idx;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. checking class ref:  \"%.*s\"..", name_size, name);
        if (base) {
            knd_log(".. base template: \"%.*s\"..", base->name_size, base->name);
        }
    }

    /* initial bulk load */
    if (task->type == KND_LOAD_STATE) {
        entry = knd_shared_dict_get(class_name_idx, name, name_size);
        if (!entry) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("failed to resolve a class ref to \"%.*s\"", name_size, name);
        }

        c = entry->class;
        if (!c) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("no such class body: \"%.*s\"", name_size, name);
        }

        if (!c->base_is_resolved) {
            err = resolve_base(c, task);
            KND_TASK_ERR("failed to resolve base classes of %.*s", name_size, name);
        }

        if (base) {
            if (!base->base_is_resolved) {
                err = resolve_base(base, task);                            RET_ERR();
            }
            if (base != c) {
                err = knd_is_base(base, c);
                if (err) {
                    knd_log("-- no inheritance from %.*s to %.*s",
                            base->name_size, base->name, c->name_size, c->name);
                    return err;
                }
            }
        }
        *result = c;
        return knd_OK;
    }

    err = knd_get_class(self->entry->repo, name, name_size, &c, task);
    KND_TASK_ERR("class \"%.*s\" not found in repo \"%.*s\"", name_size, name,
                 self->entry->repo->name_size, self->entry->repo->name);

    if (!c->base_is_resolved) {
        err = resolve_base(c, task);                                    RET_ERR();
    }

    if (base) {
        if (!base->base_is_resolved) {
            err = resolve_base(base, task);
            KND_TASK_ERR("failed to resolve class %.*s", base->name_size, base->name);
        }
        err = knd_is_base(base, c);
        KND_TASK_ERR("no inheritance from %.*s to %.*s", base->name_size, base->name, c->name_size, c->name);
    }

    *result = c;
    return knd_OK;
}

