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
#include <glb-lib/output.h>

#define DEBUG_CLASS_RESOLVE_LEVEL_1 0
#define DEBUG_CLASS_RESOLVE_LEVEL_2 0
#define DEBUG_CLASS_RESOLVE_LEVEL_3 0
#define DEBUG_CLASS_RESOLVE_LEVEL_4 0
#define DEBUG_CLASS_RESOLVE_LEVEL_5 0
#define DEBUG_CLASS_RESOLVE_LEVEL_TMP 1

static int inherit_attr(void *obj,
                        const char *unused_var(elem_id),
                        size_t unused_var(elem_id_size),
                        size_t unused_var(count),
                        void *elem)
{
    struct kndTask *task = obj;
    struct kndMemPool *mempool = task->mempool;
    struct kndClass *self = task->class;
    struct kndSet     *attr_idx = task->class->attr_idx;
    struct kndAttrRef *src_ref = elem;
    struct kndAttr    *attr    = src_ref->attr;
    struct kndAttrRef *ref;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) 
        knd_log("..  \"%.*s\" attr inherited by %.*s..",
                attr->name_size, attr->name,
                self->name_size, self->name);

    err = knd_attr_ref_new(mempool, &ref);                                        RET_ERR();
    ref->attr = attr;
    ref->attr_var = src_ref->attr_var;
    ref->class_entry = src_ref->class_entry;

    err = attr_idx->add(attr_idx,
                        attr->id, attr->id_size,
                        (void*)ref);                                              RET_ERR();
    if (ref->attr_var) {
        if (attr->is_indexed) {
            if (DEBUG_CLASS_RESOLVE_LEVEL_2) 
                knd_log("..  indexing \"%.*s\" attr var in %.*s..",
                        attr->name_size, attr->name,
                        self->name_size, self->name);
            if (attr->is_a_set) {
                err = knd_index_attr_var_list(self, attr, ref->attr_var, task);   RET_ERR();
            }
        }
    }
    return knd_OK;
}

static int inherit_attrs(struct kndClass *self, struct kndClass *base,
                         struct kndTask *task)
{
    int err;

    if (!base->is_resolved) {
        err = knd_class_resolve(base, task);                                      RET_ERR();
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. \"%.*s\" class to inherit attrs from \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                base->name_size, base->name);
    }
    task->class = self;
    err = base->attr_idx->map(base->attr_idx,
                              inherit_attr,
                              (void*)task);                                        RET_ERR();
    return knd_OK;
}


static int link_ancestor(struct kndClass *self,
                         struct kndClassEntry *base_entry,
                         struct kndTask *task)
{
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *prev_entry;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *desc_idx;
    struct kndClass *base;
    struct kndClassRef *ref;
    struct ooDict *class_name_idx = self->entry->repo->class_name_idx;
    void *result;
    int err;

    base = base_entry->class;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. linking ancestor: \"%.*s\" top:%d",
                base->name_size, base->name, base->state_top);

    if (base_entry->repo != entry->repo) {
        prev_entry = class_name_idx->get(class_name_idx,
                                         base_entry->name,
                                         base_entry->name_size);
        if (prev_entry) {
            base = prev_entry->class;
        } else {
            knd_log("-- class \"%.*s\" not found in repo \"%.*s\"",
                    base_entry->name_size, base_entry->name,
                    self->entry->repo->name_size, self->entry->repo->name);

            err = knd_class_clone(base_entry->class, self->entry->repo, &base, mempool);   RET_ERR();
        }
    }

    desc_idx = base->entry->descendants;
    if (!desc_idx) {
        err = knd_set_new(mempool, &desc_idx);                                    RET_ERR();
        desc_idx->type = KND_SET_CLASS;
        desc_idx->base = base->entry;
        base->entry->descendants = desc_idx;
    }
 
    err = desc_idx->get(desc_idx, entry->id, entry->id_size, &result);
    if (!err) {
        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            knd_log("== link already established between %.*s and %.*s",
                    entry->name_size, entry->name,
                    base->entry->name_size,
                    base->entry->name);
        return knd_OK;
    }

    /* add an ancestor */
    err = knd_class_ref_new(mempool, &ref);                                   RET_ERR();
    ref->class = base;
    ref->entry = base->entry;
    ref->next = entry->ancestors;
    entry->ancestors = ref;
    entry->num_ancestors++;

    base->entry->num_terminals++;

    /* register as a descendant */
    err = desc_idx->add(desc_idx, entry->id, entry->id_size,
                        (void*)entry);                                             RET_ERR();

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. add \"%.*s\" (repo:%.*s) as "
                " a descendant of ancestor \"%.*s\" (repo:%.*s)..",
                entry->name_size, entry->name,
                entry->repo->name_size, entry->repo->name,
                base->entry->name_size, base->entry->name,
                base->entry->repo->name_size, base->entry->repo->name);

    return knd_OK;
}

static inline void link_child(struct kndClassEntry *self,
                              struct kndClassRef *child_ref)
{
    child_ref->next = self->children;
    // atomic
    self->children = child_ref;
}

static int link_baseclass(struct kndClass *self,
                          struct kndClass *base,
                          struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClassRef *ref, *baseref;
    struct kndClass *base_copy = NULL;
    struct kndClassEntry *entry = self->entry;
    struct kndRepo *repo = self->entry->repo;
    struct kndSet *desc_idx;
    bool parent_linked = false;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. \"%.*s\" (%.*s) links to base => \"%.*s\" (%.*s) top:%d",
                entry->name_size, entry->name,
                entry->repo->name_size, entry->repo->name,
                base->entry->name_size, base->entry->name,
                base->entry->repo->name_size, base->entry->repo->name,
                base->entry->class->state_top);

    if (base->entry->repo != repo) {
        err = knd_class_clone(base, repo, &base_copy, mempool);                   RET_ERR();
        base = base_copy;
        err = link_ancestor(self, base->entry, task);                             RET_ERR();
        parent_linked = true;
    }

    /* register as a child */
    err = knd_class_ref_new(mempool, &ref);                                       RET_ERR();
    ref->entry = entry;
    ref->class = self;
    link_child(base->entry, ref);
    if (task->batch_mode)
        base->entry->num_children++;

    /* copy the ancestors */
    for (baseref = base->entry->ancestors; baseref; baseref = baseref->next) {
        if (baseref->entry->class && baseref->entry->class->state_top) continue;
        err = link_ancestor(self, baseref->entry, task);                          RET_ERR();
    }

    if (!parent_linked) {
        /* register a parent */
        err = knd_class_ref_new(mempool, &ref);                                   RET_ERR();
        ref->class = base;
        ref->entry = base->entry;
        ref->next = entry->ancestors;
        entry->ancestors = ref;
        entry->num_ancestors++;
        base->entry->num_terminals++;

        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            knd_log(".. add \"%.*s\" (repo:%.*s) as a child of \"%.*s\" (repo:%.*s)..",
                    entry->name_size, entry->name,
                    entry->repo->name_size, entry->repo->name,
                    base->entry->name_size, base->entry->name,
                    base->entry->repo->name_size, base->entry->repo->name);

        /* register a descendant */
        desc_idx = base->entry->descendants;
        if (!desc_idx) {
            err = knd_set_new(mempool, &desc_idx);                                RET_ERR();
            desc_idx->type = KND_SET_CLASS;
            desc_idx->base = base->entry;
            base->entry->descendants = desc_idx;
        }
        err = desc_idx->add(desc_idx, entry->id, entry->id_size,
                            (void*)entry);                                        RET_ERR();
    }

    return knd_OK;
}

static int resolve_baseclasses(struct kndClass *self,
                               struct kndTask *task)
{
    struct kndClassVar *cvar;
    struct kndClassEntry *entry;
    struct kndClass *c = NULL;
    struct glbOutput *log = task->log;
    const char *classname;
    size_t classname_size;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. class \"%.*s\" to resolve its bases..",
                self->name_size, self->name);

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
                knd_log("-- no base class specified in class cvar \"%.*s\"",
                        self->entry->name_size, self->entry->name);
                return knd_FAIL;
            }
            err = knd_get_class(self->entry->repo,
                                classname, classname_size, &c, task);
            if (err) {
                knd_log("-- no such class: %.*s", classname_size, classname);
                log->reset(log);
                log->writef(log, "%.*s class name not found",
                            classname_size, classname);
                return err;
            }
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
        err = inherit_attrs(self, c, task);                                       RET_ERR();

        err = link_baseclass(self, c, task);                                      RET_ERR();
        cvar->entry->class = c;
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log("++ \"%.*s\" has resolved its baseclasses!",
                self->name_size, self->name);
        self->str(self, 1);
    }

    return knd_OK;
}

extern int knd_class_resolve(struct kndClass *self,
                             struct kndTask *task)
{
    struct kndClassVar *cvar;
    struct kndClassEntry *entry;
    struct kndRepo *repo = self->entry->repo;
    int err;

    if (self->is_resolved) {
        /*if (self->attr_var_inbox_size) {
            err = knd_apply_attr_var_updates(self, class_update);                 RET_ERR();
            }*/

        return knd_OK;
    }

    repo->num_classes++;
    entry = self->entry;
    entry->numid = repo->num_classes;
    knd_uid_create(entry->numid, entry->id, &entry->id_size);

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. resolving class \"%.*s\" id:%.*s entry numid:%zu task:%zu",
                self->entry->name_size, self->entry->name,
                entry->id_size, entry->id, self->entry->numid, task->id);
    }

    /* a child of the root class */
    if (!self->baseclass_vars) {
        err = link_baseclass(self, repo->root_class, task);                       RET_ERR();
    } else {
        err = resolve_baseclasses(self, task);                                    RET_ERR();
        for (cvar = self->baseclass_vars; cvar; cvar = cvar->next) {
            if (cvar->attrs) {
                err = knd_resolve_attr_vars(self, cvar, task);                    RET_ERR();
            }
        }
    }
    /* primary attrs */
    if (self->num_attrs) {
        err = knd_resolve_primary_attrs(self, task);                              RET_ERR();
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log("++ class \"%.*s\" id:%.*s resolved!",
            self->entry->name_size, self->entry->name,
            self->entry->id_size, self->entry->id);

    self->is_resolved = true;
    return knd_OK;
}

int knd_resolve_class_ref(struct kndClass *self,
                          const char *name, size_t name_size,
                          struct kndClass *base,
                          struct kndClass **result,
                          struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndClass *c;
    struct ooDict *class_name_idx = self->entry->repo->class_name_idx;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. checking class ref:  \"%.*s\"..",
                name_size, name);
        if (base) {
            knd_log(".. base template: \"%.*s\"..",
                    base->name_size, base->name);
        }
    }

    if (task->batch_mode) {
        entry = class_name_idx->get(class_name_idx,
                                    name, name_size);
        if (!entry) {
            knd_log("-- couldn't resolve the class ref \"%.*s\"",
                    name_size, name);
            return knd_FAIL;
        }
        c = entry->class;
        if (!c) {
            knd_log("-- no such class: \"%.*s\"", name_size, name);
            return knd_FAIL;
        }
        if (!c->is_resolved) {
            err = knd_class_resolve(c, task);                                   RET_ERR();
        }

        if (base) {
            err = knd_is_base(base, c);
            if (err) {
                knd_log("-- no inheritance from %.*s to %.*s",
                        base->name_size, base->name,
                        c->name_size, c->name);
                return err;
            }
        }
        *result = c;
        return knd_OK;
    }

    err = knd_get_class(self->entry->repo, name, name_size, &c, task);
    if (err) {
        knd_log("-- no such class: %.*s [repo:%.*s]",
                name_size, name,
                self->entry->repo->name_size,
                self->entry->repo->name);
        return err;
    }

    if (!c->is_resolved) {
        err = knd_class_resolve(c, task);                                                RET_ERR();
    }

    if (base) {
        err = knd_is_base(base, c);
        if (err) {
            knd_log("-- no inheritance from %.*s to %.*s",
                    base->name_size, base->name,
                    c->name_size, c->name);
            return err;
        }
    }

    *result = c;
    return knd_OK;
}

int knd_resolve_classes(struct kndClass *self,
                        struct kndTask *task)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndSet *class_idx = self->entry->repo->class_idx;
    struct ooDict *class_name_idx = self->entry->repo->class_name_idx;
    const char *key;
    void *val;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_TMP)
        knd_log(".. resolving classes in \"%.*s\"  idx:%p",
                self->entry->name_size, self->entry->name, class_name_idx);

    key = NULL;
    class_name_idx->rewind(class_name_idx);
    do {
        class_name_idx->next_item(class_name_idx, &key, &val);
        if (!key) break;
        entry = (struct kndClassEntry*)val;
        if (!entry->class) {
            knd_log("-- unresolved class entry: %.*s",
                    entry->name_size, entry->name);
            return knd_FAIL;
        }
        c = entry->class;

        if (!c->is_resolved) {
            err = knd_class_resolve(c, task);
            if (err) {
                knd_log("-- couldn't resolve the \"%.*s\" class",
                        c->entry->name_size, c->entry->name);
                return err;
            }
            c->is_resolved = true;
        }

        err = class_idx->add(class_idx,
                             c->entry->id, c->entry->id_size,
                             (void*)c->entry);
        if (err) return err;

        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
                c->str(c, 1);
        }

    } while (key);

    return knd_OK;
}
