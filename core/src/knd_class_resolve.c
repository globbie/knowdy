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

extern int knd_inherit_attrs(struct kndClass *self, struct kndClass *base)
{
    struct kndClassEntry *entry;
    struct kndAttr *attr;
    struct kndClass *c;
    struct kndAttrEntry *attr_entry;
    struct kndClassVar *item;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. \"%.*s\" class to inherit attrs from \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                base->name_size, base->name);
    }

    if (!base->is_resolved) {
        err = base->resolve(base, NULL);                                          RET_ERR();
    }

    /* check circled relations */
    for (size_t i = 0; i < self->num_bases; i++) {
        entry = self->bases[i];
        c = entry->class;

        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            knd_log("== (%zu of %zu)  \"%.*s\" is a base of \"%.*s\"",
                    i, self->num_bases, c->name_size, c->name,
                    self->entry->name_size, self->entry->name);

        if (entry->class == base) {
            if (DEBUG_CLASS_RESOLVE_LEVEL_2)
                knd_log("NB: class \"%.*s\" is already inherited "
                        "by \"%.*s\"",
                        base->name_size, base->name,
                        self->entry->name_size, self->entry->name);
            return knd_OK;
        }
    }

    /* get attrs from base */
    for (attr = base->attrs; attr; attr = attr->next) {

        /* compare with local set of attrs */
        attr_entry = self->attr_name_idx->get(self->attr_name_idx,
                                              attr->name, attr->name_size);
        if (attr_entry) {
            knd_log("-- %.*s attr collision between \"%.*s\" and base class \"%.*s\"?",
                    attr_entry->name_size, attr_entry->name,
                    self->entry->name_size, self->entry->name,
                    base->name_size, base->name);
            return knd_FAIL;
        }

        /* register attr entry */
        attr_entry = malloc(sizeof(struct kndAttrEntry));
        if (!attr_entry) return knd_NOMEM;
        memset(attr_entry, 0, sizeof(struct kndAttrEntry));
        memcpy(attr_entry->name, attr->name, attr->name_size);
        attr_entry->name_size = attr->name_size;
        attr_entry->attr = attr;

        err = self->attr_name_idx->set(self->attr_name_idx,
                                       attr_entry->name, attr_entry->name_size,
                                       (void*)attr_entry);
        if (err) return err;

        /* computed attrs */
        if (attr->proc) {
            self->computed_attrs[self->num_computed_attrs] = attr;
            self->num_computed_attrs++;
        }
    }

    if (self->num_bases >= KND_MAX_BASES) {
        knd_log("-- max bases exceeded for %.*s :(",
                self->entry->name_size, self->entry->name);

        return knd_FAIL;
    }
    self->bases[self->num_bases] = base->entry;
    self->num_bases++;

    if (DEBUG_CLASS_RESOLVE_LEVEL_1)
        knd_log(" .. add %.*s parent to %.*s",
                base->entry->class->name_size,
                base->entry->class->name,
                self->entry->name_size, self->entry->name);

    /* contact the grandparents */
    for (item = base->baseclass_vars; item; item = item->next) {
        err = knd_inherit_attrs(self, item->entry->class);                            RET_ERR();
    }

    return knd_OK;
}

static int index_attr(struct kndClass *self,
                      struct kndAttr *attr,
                      struct kndAttrVar *item)
{
    struct kndClass *base;
    struct kndSet *set;
    struct kndClass *c = NULL;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log("\n.. indexing CURR CLASS: \"%.*s\" .. index attr: \"%.*s\" [type:%d]"
                " refclass: \"%.*s\" (name:%.*s val:%.*s)",
                self->entry->name_size, self->entry->name,
                attr->name_size, attr->name, attr->type,
                attr->ref_classname_size, attr->ref_classname,
                item->name_size, item->name, item->val_size, item->val);
    }

    if (!attr->ref_classname_size) return knd_OK;

    /* template base class */
    err = knd_get_class(self,
                        attr->ref_classname,
                        attr->ref_classname_size,
                        &base);                                                       RET_ERR();
    if (!base->is_resolved) {
        err = base->resolve(base, NULL);                                          RET_ERR();
    }

    /* specific class */
    err = knd_get_class(self,
                        item->val,
                        item->val_size, &c);                                          RET_ERR();

    item->class = c;

    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }
    err = knd_is_base(base, c);                                                       RET_ERR();

    set = attr->parent_class->entry->descendants;

    /* add curr class to the reverse index */
    err = set->add_ref(set, attr, self->entry, c->entry);
    if (err) return err;

    return knd_OK;
}

static int index_attr_var_list(struct kndClass *self,
                                struct kndAttr *attr,
                                struct kndAttrVar *parent_item)
{
    struct kndClass *base;
    struct kndSet *set;
    struct kndClass *c = NULL;
    struct kndAttrVar *item = parent_item;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log("\n.. attr item list indexing.. (class:%.*s) .. index attr: \"%.*s\" [type:%d]"
                " refclass: \"%.*s\" (name:%.*s val:%.*s)",
                self->entry->name_size, self->entry->name,
                attr->name_size, attr->name, attr->type,
                attr->ref_classname_size, attr->ref_classname,
                item->name_size, item->name, item->val_size, item->val);
    }

    if (!attr->ref_classname_size) return knd_OK;

    /* template base class */
    err = knd_get_class(self,
                    attr->ref_classname,
                    attr->ref_classname_size,
                    &base);                                                       RET_ERR();
    if (!base->is_resolved) {
        err = base->resolve(base, NULL);                                          RET_ERR();
    }

    for (item = parent_item->list; item; item = item->next) {

        if (DEBUG_CLASS_RESOLVE_LEVEL_3)
            knd_log("== list item name:%.*s", item->name_size, item->name);

        /* specific class */
        err = knd_get_class(self,
                        item->name,
                        item->name_size, &c);                                          RET_ERR();
        item->class = c;

        if (!c->is_resolved) {
            err = c->resolve(c, NULL);                                                RET_ERR();
        }
        err = knd_is_base(base, c);                                                       RET_ERR();

        set = attr->parent_class->entry->descendants;

        /* add curr class to the reverse index */
        err = set->add_ref(set, attr, self->entry, c->entry);
        if (err) return err;
    }
    return knd_OK;
}

static int resolve_class_ref(struct kndClass *self,
                             const char *name, size_t name_size,
                             struct kndClass *base,
                             struct kndClass **result)
{
    struct kndClassEntry *entry;
    struct kndClass *c;
    struct ooDict *class_name_idx = self->entry->repo->root_class->class_name_idx;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. checking class ref:  %.*s", name_size, name);

    entry = class_name_idx->get(class_name_idx, name, name_size);
    if (!entry) {
        knd_log("-- no such class: \"%.*s\"", name_size, name);
        //return knd_OK;
        return knd_FAIL;
    }

    /* class could be frozen */
    if (!entry->class) {
        if (!entry->block_size) return knd_FAIL;
        //err = unfreeze_class(self, entry, &entry->class);
        //if (err) return err;
    }
    c = entry->class;

    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }

    if (base) {
        err = knd_is_base(base, c);                                                   RET_ERR();
    }

    *result = c;
    return knd_OK;
}

static int resolve_proc_ref(struct kndClass *self,
                             const char *name, size_t name_size,
                             struct kndProc *base __attribute__((unused)),
                             struct kndProc **result)
{
    struct kndProc *root_proc, *proc;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. resolving proc ref:  %.*s", name_size, name);

    root_proc = self->entry->repo->root_proc;
    err = root_proc->get_proc(root_proc,
                              name, name_size, &proc);                            RET_ERR();

    /*c = dir->conc;
    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }

    if (base) {
        err = is_base(base, c);                                                   RET_ERR();
    }
    */

    *result = proc;

    return knd_OK;
}

static int resolve_aggr_item(struct kndClass *self,
                             struct kndAttrVar *parent_item)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndClass *c;
    struct kndAttrVar *item;
    struct kndAttr *attr;
    const char *classname;
    size_t classname_size;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. resolve aggr item %.*s  attr:%p",
                parent_item->name_size, parent_item->name,
                parent_item->attr);

    if (!parent_item->attr->conc) {
        err = resolve_class_ref(self,
                                parent_item->attr->ref_classname,
                                parent_item->attr->ref_classname_size,
                                NULL, &parent_item->attr->conc);
        if (err) return err;
    }
    c = parent_item->attr->conc;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. resolving aggr item \"%.*s\" (count:%zu)"
                " class:%.*s  is_list_item:%d",
                parent_item->name_size,  parent_item->name,
                parent_item->list_count,
                c->name_size, c->name, parent_item->is_list_item);
    }

    classname = parent_item->val;
    classname_size = parent_item->val_size;
    if (parent_item->is_list_item) {
        classname = parent_item->name;
        classname_size = parent_item->name_size;
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        c->str(c);

    if (c->implied_attr) {
        attr = c->implied_attr;

        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            knd_log("== class: \"%.*s\" implied attr: %.*s",
                    classname_size, classname,
                    attr->name_size, attr->name);

        parent_item->implied_attr = attr;

        switch (attr->type) {
        case KND_ATTR_NUM:

            if (DEBUG_CLASS_RESOLVE_LEVEL_2)
                knd_log(".. resolving implied num attr: %.*s val:%.*s",
                        parent_item->name_size, parent_item->name,
                        parent_item->val_size, parent_item->val);

            if (parent_item->val_size) {
                memcpy(buf, parent_item->val, parent_item->val_size);
                buf_size = parent_item->val_size;
                buf[buf_size] = '\0';

                err = knd_parse_num(buf, &parent_item->numval);
                // TODO: float parsing
            }
            break;
        case KND_ATTR_AGGR:
            break;
        case KND_ATTR_REF:
            err = resolve_class_ref(self,
                                    classname, classname_size,
                                    attr->conc, &parent_item->class);
            if (err) return err;
            break;
        default:
            break;
        }
    }

    /* resolve nested children */
    for (item = parent_item->children; item; item = item->next) {
        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
            knd_log(".. check attr \"%.*s\" in class \"%.*s\" "
                    " is_resolved:%d",
                    item->name_size, item->name,
                    c->name_size, c->name, c->is_resolved);
        }

        err = knd_class_get_attr(c, item->name, item->name_size, &attr);
        if (err) {
            knd_log("-- no attr \"%.*s\" in class \"%.*s\" :(",
                    item->name_size, item->name,
                    c->name_size, c->name);
            return err;
        }
        item->attr = attr;

        switch (attr->type) {
        case KND_ATTR_NUM:
            if (DEBUG_CLASS_RESOLVE_LEVEL_2)
                knd_log(".. resolving default num attr: %.*s val:%.*s",
                        item->name_size, item->name,
                        item->val_size, item->val);

            memcpy(buf, item->val, item->val_size);
            buf_size = item->val_size;
            buf[buf_size] = '\0';
            err = knd_parse_num(buf, &item->numval);

            break;
        case KND_ATTR_AGGR:
            if (DEBUG_CLASS_RESOLVE_LEVEL_2)
                knd_log("== nested aggr item found: %.*s conc:%p",
                        item->name_size, item->name, attr->conc);
            err = resolve_aggr_item(self, item);
            if (err) return err;
            break;
        case KND_ATTR_REF:

            classname = item->val;
            classname_size = item->val_size;

            err = resolve_class_ref(self,
                                    classname, classname_size,
                                    attr->conc, &item->class);
            if (err) return err;

            break;
        default:
            break;
        }
    }

    return knd_OK;
}

static int resolve_attr_var_list(struct kndClass *self,
                                  struct kndAttrVar *parent_item)
{
    struct kndAttr *parent_attr = parent_item->attr;
    struct kndAttrVar *item;
    struct kndClass *c;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        const char *attr_type_name = knd_attr_names[parent_attr->type];
        size_t attr_type_name_size = strlen(attr_type_name);
        knd_log(".. class %.*s to resolve attr item list \"%.*s\" "
                " of CLASS:%.*s   attr type:%.*s",
                self->entry->name_size, self->entry->name,
                parent_item->name_size, parent_item->name,
                parent_attr->ref_classname_size,
                parent_attr->ref_classname,
                attr_type_name_size, attr_type_name);
    }

    if (!parent_attr->conc) {
        err = resolve_class_ref(self,
                                parent_attr->ref_classname,
                                parent_attr->ref_classname_size,
                                NULL, &parent_attr->conc);
        if (err) return err;
    }

    /* base template class */
    c = parent_attr->conc;
    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        c->str(c);

    /* first item */
    if (parent_item->val_size) {
        parent_item->attr = parent_attr;

        switch (parent_attr->type) {
        case KND_ATTR_AGGR:
            err = resolve_aggr_item(self, parent_item);
            if (err) {
                knd_log("-- first aggr item not resolved :(");
                return err;
            }
            break;
        case KND_ATTR_REF:
            err = resolve_class_ref(self,
                                    parent_item->val, parent_item->val_size,
                                    c, &parent_item->class);
            if (err) return err;
            break;
        default:
            break;
        }
    }

    for (item = parent_item->list; item; item = item->next) {
        item->attr = parent_attr;


        switch (parent_attr->type) {
        case KND_ATTR_AGGR:
            err = resolve_aggr_item(self, item);
            if (err) return err;
            break;
        case KND_ATTR_REF:

            if (item->val_size) {
                err = resolve_class_ref(self, item->val, item->val_size,
                                        c, &item->class);
                if (err) return err;
            } else {
                err = resolve_class_ref(self, item->name, item->name_size,
                                        c, &item->class);
                if (err) return err;
            }

            break;
        default:
            break;
        }
    }

    return knd_OK;
}

static int resolve_attr_vars(struct kndClass *self,
                             struct kndClassVar *parent_item)
{
    struct kndAttrVar *attr_var;
    struct kndAttrEntry *entry;
    struct kndClass *c;
    struct kndProc *proc;
    struct ooDict *attr_name_idx = self->attr_name_idx;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. resolving attr vars of class %.*s",
                self->entry->name_size, self->entry->name);
    }

    for (attr_var = parent_item->attrs; attr_var; attr_var = attr_var->next) {
        entry = attr_name_idx->get(attr_name_idx,
                                   attr_var->name, attr_var->name_size);
        if (!entry) {
            knd_log("-- no such attr: %.*s", attr_var->name_size, attr_var->name);
            return knd_FAIL;
        }

        /* save attr assignment */
        entry->attr_var = attr_var;

        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
            knd_log("== entry attr: %.*s %d",
                    attr_var->name_size, attr_var->name,
                    entry->attr->is_indexed);
        }

        if (entry->attr->is_a_set) {
            attr_var->attr = entry->attr;

            err = resolve_attr_var_list(self, attr_var);
            if (err) return err;
            if (attr_var->val_size)
                attr_var->num_list_elems++;

            if (entry->attr->is_indexed) {
                err = index_attr_var_list(self, entry->attr, attr_var);
                if (err) return err;
            }
            continue;
        }

        /* single attr */
        switch (entry->attr->type) {
        case KND_ATTR_AGGR:
            /* TODO */
            attr_var->attr = entry->attr;
            err = resolve_aggr_item(self, attr_var);
            if (err) {
                knd_log("-- aggr attr_var not resolved :(");
                return err;
            }

            break;
        case KND_ATTR_REF:
            c = entry->attr->conc;
            if (!c->is_resolved) {
                err = c->resolve(c, NULL);                                        RET_ERR();
            }
            err = resolve_class_ref(self, attr_var->val, attr_var->val_size,
                                    c, &attr_var->class);
            if (err) return err;
            break;
        case KND_ATTR_PROC:
            proc = entry->attr->proc;
            /*if (!c->is_resolved) {
                err = c->resolve(c, NULL);                                        RET_ERR();
                }*/
            err = resolve_proc_ref(self, attr_var->val, attr_var->val_size,
                                   proc, &attr_var->proc);
            if (err) return err;
            break;
        default:
            /* atomic value, call a validation function? */
            break;
        }

        if (entry->attr->is_indexed) {
            err = index_attr(self, entry->attr, attr_var);
            if (err) return err;
        }
        attr_var->attr = entry->attr;
    }

    return knd_OK;
}

static int resolve_attrs(struct kndClass *self)
{
    struct kndAttr *attr;
    struct kndAttrEntry *attr_entry;
    struct kndClassEntry *entry;
    struct kndProc *root_proc;
    struct kndProcEntry *proc_entry;
    struct ooDict *class_name_idx = self->entry->repo->root_class->class_name_idx;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. resolving attrs..");

    // TODO: mempool
    err = ooDict_new(&self->attr_name_idx, KND_SMALL_DICT_SIZE);                       RET_ERR();

    for (attr = self->attrs; attr; attr = attr->next) {
        attr_entry = self->attr_name_idx->get(self->attr_name_idx, attr->name, attr->name_size);
        if (attr_entry) {
            knd_log("-- %.*s attr already exists?", attr->name_size, attr->name);
            return knd_FAIL;
        }

        /* TODO: mempool */
        attr_entry = malloc(sizeof(struct kndAttrEntry));
        if (!attr_entry) return knd_NOMEM;

        memset(attr_entry, 0, sizeof(struct kndAttrEntry));
        memcpy(attr_entry->name, attr->name, attr->name_size);
        attr_entry->name_size = attr->name_size;
        attr_entry->attr = attr;

        err = self->attr_name_idx->set(self->attr_name_idx,
                                       attr_entry->name, attr_entry->name_size,
                                       (void*)attr_entry);                        RET_ERR();

        /* computed attr idx */
        if (attr->proc) {
            self->computed_attrs[self->num_computed_attrs] = attr;
            self->num_computed_attrs++;
        }

        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            knd_log("++ register primary attr: \"%.*s\"",
                    attr->name_size, attr->name);

        switch (attr->type) {
        case KND_ATTR_AGGR:
        case KND_ATTR_REF:
            if (!attr->ref_classname_size) {
                knd_log("-- no classname specified for attr \"%s\"",
                        attr->name);
                return knd_FAIL;
            }

            entry = class_name_idx->get(class_name_idx,
                                        attr->ref_classname,
                                        attr->ref_classname_size);
            if (!entry) {
                knd_log("-- no such class: \"%.*s\" .."
                        "couldn't resolve the \"%.*s\" attr of %.*s :(",
                        attr->ref_classname_size,
                        attr->ref_classname,
                        attr->name_size, attr->name,
                        self->entry->name_size, self->entry->name);
                return knd_FAIL;
            }
            attr->conc = entry->class;
            break;
        case KND_ATTR_PROC:
            if (!attr->ref_procname_size) {
                knd_log("-- no proc name specified for attr \"%s\"",
                        attr->name);
                return knd_FAIL;
            }

            root_proc = self->entry->repo->root_proc;
            proc_entry = root_proc->proc_name_idx->get(root_proc->proc_name_idx,
                                                       attr->ref_procname,
                                                       attr->ref_procname_size);
            if (!proc_entry) {
                knd_log("-- no such proc: \"%.*s\" .."
                        "couldn't resolve the \"%.*s\" attr of %.*s :(",
                        attr->ref_procname_size,
                        attr->ref_procname,
                        attr->name_size, attr->name,
                        self->entry->name_size, self->entry->name);
                return knd_FAIL;
            }
            // TODO
            //attr->proc = proc_entry->proc;

            if (DEBUG_CLASS_RESOLVE_LEVEL_2)
                knd_log("++ proc ref resolved: %.*s!",
                        proc_entry->name_size, proc_entry->name);
            break;
        default:
            break;
        }

        if (attr->is_implied)
            self->implied_attr = attr;
    }

    return knd_OK;
}

static int resolve_objs(struct kndClass     *self,
                        struct kndClassUpdate *class_update)
{
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndClassInst *obj;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_1)
        knd_log("..resolving objs, num objs: %zu",
                self->obj_inbox_size);

    if (class_update) {
        class_update->insts = calloc(self->obj_inbox_size,
                                     sizeof(struct kndClassInst*));
        if (!class_update->insts) {
            return knd_NOMEM;
        }

        err = mempool->new_state(mempool, &state);                                MEMPOOL_ERR(ClassInstState);
        state->update = class_update->update;
        state->val = (void*)class_update;
        state->next = self->inst_states;
        self->inst_states = state;
        self->num_inst_states++;
        state->numid = self->num_inst_states;
    }

    for (obj = self->obj_inbox; obj; obj = obj->next) {
        if (obj->states->phase == KND_REMOVED) {
            knd_log("NB: \"%.*s\" obj to be removed", obj->name_size, obj->name);
            goto update;
        }
        err = obj->resolve(obj);
        if (err) {
            knd_log("-- %.*s obj not resolved :(",
                    obj->name_size, obj->name);
            goto final;
        }
        obj->states->phase = KND_CREATED;
    update:
        if (class_update) {
            /* NB: should never happen: mismatch of num objs */
            if (class_update->num_insts >= self->obj_inbox_size) {
                knd_log("-- num objs mismatch in %.*s:  %zu vs %zu:(",
                        self->entry->name_size, self->entry->name,
                        class_update->num_insts, self->obj_inbox_size);
                return knd_FAIL;
            }
            class_update->insts[class_update->num_insts] = obj;
            class_update->num_insts++;
            obj->states->update = class_update->update;
            obj->states->val = (void*)class_update;
        }
    }
    err =  knd_OK;

 final:

    return err;
}

static int register_descendants(struct kndClass *self)
{
    struct kndClassEntry *entry;
    struct kndSet *set;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;

    for (size_t i = 0; i < self->num_bases; i++) {
        entry = self->bases[i];

        set = entry->descendants;
        if (!set) {
            err = mempool->new_set(mempool, &set);                    RET_ERR();
            set->type = KND_SET_CLASS;
            set->base = entry;
            entry->descendants = set;
        }

        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            knd_log(".. add \"%.*s\" as a descendant of \"%.*s\"..",
                    self->entry->name_size, self->entry->name,
                    entry->name_size, entry->name);

        err = set->add(set, self->entry->id, self->entry->id_size,
                       (void*)self->entry);                                       RET_ERR();
    }
    return knd_OK;
}

static int resolve_baseclasses(struct kndClass *self)
{
    struct kndClassVar *cvar;
    void *result;
    struct kndClassEntry *entry;
    struct kndClass *c = NULL;
    const char *classname;
    size_t classname_size;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. \"%.*s\" to resolve its baseclasses..",
                self->name_size, self->name);

    /* resolve refs to base classes */
    for (cvar = self->baseclass_vars; cvar; cvar = cvar->next) {

        if (cvar->entry->class == self) {
            /* TODO */
            if (DEBUG_CLASS_RESOLVE_LEVEL_2)
                knd_log(".. \"%.*s\" class to check the update request: \"%s\"..",
                        self->entry->name_size, self->entry->name,
                        cvar->entry->name_size, cvar->entry->name);
            continue;
        }

        classname = cvar->entry->name;
        classname_size = cvar->entry->name_size;

        if (cvar->id_size) {
            err = self->class_idx->get(self->class_idx,
                                       cvar->id, cvar->id_size, &result);
            if (err) {
                knd_log("-- no such class: %.*s :(",
                        cvar->id_size, cvar->id);
                return knd_FAIL;
            }
            entry = result;
            cvar->entry = entry;

            //memcpy(cvar->entry->name, entry->name, entry->name_size);
            //cvar->classname_size = entry->name_size;

            classname = entry->name;
            classname_size = entry->name_size;
        }

        if (!classname_size) {
            knd_log("-- no base class specified in class cvar \"%.*s\" :(",
                    self->entry->name_size, self->entry->name);
            return knd_FAIL;
        }

        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            knd_log(".. \"%.*s\" class to get its base class: \"%.*s\"..",
                    self->entry->name_size, self->entry->name,
                    classname_size, classname);

        err = knd_get_class(self, classname, classname_size, &c);         RET_ERR();

        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            c->str(c);

        if (c == self) {
            knd_log("-- self reference detected in \"%.*s\" :(",
                    cvar->entry->name_size, cvar->entry->name);
            return knd_FAIL;
        }

        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
            knd_log("++ \"%.*s\" ref established as a base class for \"%.*s\"!",
                    cvar->entry->name_size, cvar->entry->name,
                    self->entry->name_size, self->entry->name);
        }

        cvar->entry->class = c;

        /* check cvar doublets */
        for (size_t i = 0; i < self->entry->num_children; i++) {
            if (self->entry->children[i] == self->entry) {
                knd_log("-- doublet conc cvar found in \"%.*s\" :(",
                        self->entry->name_size, self->entry->name);
                return knd_FAIL;
            }
        }

        if (c->entry->num_children >= KND_MAX_CONC_CHILDREN) {
            knd_log("-- %.*s as child to %.*s - max conc children exceeded :(",
                    self->entry->name_size, self->entry->name,
                    cvar->entry->name_size, cvar->entry->name);
            return knd_FAIL;
        }

        //if (task->type == KND_UPDATE_STATE) {
        /*if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            knd_log(".. update task to add class \"%.*s\" as a child of "
                    " \"%.*s\" (total children:%zu)",
                    self->entry->name_size, self->entry->name,
                    c->name_size, c->name, c->entry->num_children);
        */

        if (!c->entry->children) {
            c->entry->children = calloc(KND_MAX_CONC_CHILDREN,
                                        sizeof(struct kndClassEntry*));
            if (!c->entry->children) {
                knd_log("-- no memory :(");
                return knd_NOMEM;
            }
        }

        if (c->entry->num_children >= KND_MAX_CONC_CHILDREN) {
            knd_log("-- warning: num of subclasses of \"%.*s\" exceeded :(",
                    c->entry->name_size, c->entry->name);
            return knd_OK;
        }
        c->entry->children[c->entry->num_children] = self->entry;
        c->entry->num_children++;

        err = knd_inherit_attrs(self, cvar->entry->class);                            RET_ERR();
    }

    /* now that we know all our base classes
       let's add a descendant to each of these */
    err = register_descendants(self);                                             RET_ERR();

    return knd_OK;
}

extern int knd_resolve_class(struct kndClass *self,
                             struct kndClassUpdate *class_update)
{
    struct kndClass *root;
    struct kndClassVar *item;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_1)
        knd_log(".. resolving class: \"%.*s\"",
                self->name_size, self->name);

    if (self->is_resolved) {
        if (self->obj_inbox_size) {
            err = resolve_objs(self, class_update);                                     RET_ERR();
        }
        return knd_OK;
    } else {
        self->entry->repo->next_class_numid++;
        entry = self->entry;
        entry->numid = self->entry->repo->next_class_numid;
        knd_num_to_str(entry->numid, entry->id, &entry->id_size, KND_RADIX_BASE);
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_1) {
        knd_log(".. resolving class \"%.*s\" id:%.*s entry numid:%zu  batch mode:%d",
                self->entry->name_size, self->entry->name,
                entry->id_size, entry->id, self->entry->numid, self->batch_mode);
    }

    /* a child of the root class
     * TODO: refset */
    if (!self->baseclass_vars) {
        root = self->root_class;
        entry = root->entry;

        if (!entry->children) {
            entry->children = calloc(KND_MAX_CONC_CHILDREN,
                                   sizeof(struct kndClassEntry*));
            if (!entry->children) {
                knd_log("-- no memory :(");
                return knd_NOMEM;
            }
        }
        if (entry->num_children >= KND_MAX_CONC_CHILDREN) {
            knd_log("-- warning: num of subclasses of \"%.*s\" exceeded :(",
                    entry->name_size, entry->name);
            return knd_OK;
        }
        entry->children[entry->num_children] = self->entry;
        entry->num_children++;
    }

    /* resolve and index the attrs */
    if (!self->attr_name_idx) {
        err = resolve_attrs(self);                                                RET_ERR();
    }

    if (self->baseclass_vars) {
        err = resolve_baseclasses(self);                                          RET_ERR();
    }

    for (item = self->baseclass_vars; item; item = item->next) {
        if (item->attrs) {
            err = resolve_attr_vars(self, item);                                  RET_ERR();
        }
    }

    if (self->obj_inbox_size) {
        err = resolve_objs(self, class_update);                                   RET_ERR();
    }

    if (class_update) {
        err = mempool->new_state(mempool, &state);                                MEMPOOL_ERR(ClassState);
        state->update = class_update->update;
        state->val = (void*)class_update;
        state->next = self->states;
        self->states = state;
        self->num_states++;
        state->numid = self->num_states;
        state->id_size = 0;
        knd_num_to_str(state->numid, state->id, &state->id_size, KND_RADIX_BASE);
    }

    self->is_resolved = true;
    return knd_OK;
}

extern int knd_resolve_classes(struct kndClass *self)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    const char *key;
    void *val;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. resolving classes in \"%.*s\"",
                self->entry->name_size, self->entry->name);

    key = NULL;
    self->class_name_idx->rewind(self->class_name_idx);
    do {
        self->class_name_idx->next_item(self->class_name_idx, &key, &val);
        if (!key) break;
        entry = (struct kndClassEntry*)val;
        if (!entry->class) {
            knd_log("-- unresolved class entry: %.*s",
                    entry->name_size, entry->name);
            return knd_FAIL;
        }
        c = entry->class;
        if (c->is_resolved) continue;

        err = c->resolve(c, NULL);
        if (err) {
            knd_log("-- couldn't resolve the \"%.*s\" class :(",
                    c->entry->name_size, c->entry->name);
            return err;
        }
        c->is_resolved = true;

        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            c->str(c);
    } while (key);

    return knd_OK;
}
