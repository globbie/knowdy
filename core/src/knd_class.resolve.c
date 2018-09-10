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

static int copy_attr_ref(void *obj,
                         const char *elem_id,
                         size_t elem_id_size,
                         size_t count,
                         void *elem)
{
    struct kndSet     *attr_idx = obj;
    struct kndAttrRef *src_ref = elem;
    struct kndAttr    *attr    = src_ref->attr;
    struct kndAttrRef *ref;
    struct kndMemPool *mempool = attr_idx->mempool;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) 
        knd_log(".. copying %.*s attr..", attr->name_size, attr->name);

    err = knd_attr_ref_new(mempool, &ref);                                        RET_ERR();
    ref->attr = attr;
    
    err = attr_idx->add(attr_idx,
                        attr->id, attr->id_size,
                        (void*)ref);                                              RET_ERR();

    return knd_OK;
}

extern int knd_inherit_attrs(struct kndClass *self, struct kndClass *base)
{
    struct kndClassEntry *entry;
    struct kndAttr *attr;
    struct kndClass *c;
    struct kndAttrRef *attr_ref;
    struct kndClassVar *item;
    struct kndSet *attr_idx = self->attr_idx;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;

    if (!base->is_resolved) {
        err = base->resolve(base, NULL);                                          RET_ERR();
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. \"%.*s\" class to inherit attrs from \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                base->name_size, base->name);
    }

    /* copy your parent's attr idx */
    self->attr_idx->mempool = self->entry->repo->mempool;
    err = base->attr_idx->map(base->attr_idx,
                              copy_attr_ref,
                              (void*)self->attr_idx);                             RET_ERR();
    
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

    if (DEBUG_CLASS_RESOLVE_LEVEL_TMP) {
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

    err = knd_is_base(base, c);                                                   RET_ERR();

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
        knd_log(".. list item base?");
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
    struct kndClass *c;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_TMP)
        knd_log("\n.. checking class ref:  \"%.*s\"", name_size, name);

    err = knd_get_class(self, name, name_size, &c);
    if (err) return err;

    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }

    if (base) {
        err = knd_is_base(base, c);                                               RET_ERR();
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

    if (DEBUG_CLASS_RESOLVE_LEVEL_TMP)
        knd_log(".. resolve aggr item %.*s..",
                parent_item->name_size, parent_item->name);

    if (!parent_item->attr->ref_class) {
        err = resolve_class_ref(self,
                                parent_item->attr->ref_classname,
                                parent_item->attr->ref_classname_size,
                                NULL, &parent_item->attr->ref_class);
        if (err) return err;
    }
    c = parent_item->attr->ref_class;
    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
        knd_log("== resolve result:%d class:%.*s [%d]",
                err, c->name_size, c->name, c->is_resolved);
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_TMP) {
        knd_log("\n.. resolving aggr item \"%.*s\" (count:%zu)"
                " class:%.*s [resolved:%d]] is_list_item:%d",
                parent_item->name_size,  parent_item->name,
                parent_item->list_count,
                c->name_size, c->name, c->is_resolved,
                parent_item->is_list_item);
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
                                    attr->ref_class, &parent_item->class);
            if (err) return err;
            break;
        default:
            break;
        }
    }

    /* resolve nested children */
    for (item = parent_item->children; item; item = item->next) {

        if (DEBUG_CLASS_RESOLVE_LEVEL_TMP) {
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
                knd_log(".. resolving default num attr: %.*s val:\"%.*s\" val size:%zu",
                        item->name_size, item->name,
                        item->val_size, item->val, item->val_size);

            memcpy(buf, item->val, item->val_size);
            buf_size = item->val_size;
            buf[buf_size] = '\0';
            err = knd_parse_num(buf, &item->numval);

            break;
        case KND_ATTR_AGGR:
            if (DEBUG_CLASS_RESOLVE_LEVEL_2)
                knd_log("== nested aggr item found: %.*s conc:%p",
                        item->name_size, item->name, attr->ref_class);
            err = resolve_aggr_item(self, item);
            if (err) return err;
            break;
        case KND_ATTR_REF:

            classname = item->val;
            classname_size = item->val_size;

            err = resolve_class_ref(self,
                                    classname, classname_size,
                                    attr->ref_class, &item->class);
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

    if (!parent_attr->ref_class) {
        err = resolve_class_ref(self,
                                parent_attr->ref_classname,
                                parent_attr->ref_classname_size,
                                NULL, &parent_attr->ref_class);
        if (err) return err;
    }

    /* base template class */
    c = parent_attr->ref_class;
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
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    struct kndClass *c;
    struct kndProc *proc;
    struct kndSet *attr_idx = self->attr_idx;
    struct kndRepo *repo = self->entry->repo;
    struct ooDict *attr_name_idx = repo->attr_name_idx;
    struct glbOutput *log = repo->log;
    struct kndTask *task = repo->task;
    int e, err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_TMP) {
        knd_log("\n.. resolving attr vars of class \"%.*s\"..",
                self->entry->name_size, self->entry->name);
    }

    for (attr_var = parent_item->attrs; attr_var; attr_var = attr_var->next) {
        if (DEBUG_CLASS_RESOLVE_LEVEL_TMP) {
            knd_log(".. resolving attr var: %.*s",
                    attr_var->name_size, attr_var->name);
        }
        err = knd_class_get_attr(self,
                                 attr_var->name, attr_var->name_size, &attr);
        if (err) {
            knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                    attr_var->name_size, attr_var->name,
                    self->name_size, self->name);
            return err;
        }
        
        /*attr_ref = attr_name_idx->get(attr_name_idx,
                                      attr_var->name, attr_var->name_size);
        if (!attr_ref) {
            knd_log("-- no such attr: %.*s",
                    attr_var->name_size, attr_var->name);
            log->reset(log);
            e = log->write(log, attr_var->name, attr_var->name_size);
            if (e) return e;
            e = log->write(log, ": no such attribute",
                           strlen(": no such attribute"));
            if (e) return e;
            task->http_code = HTTP_NOT_FOUND;
            return knd_FAIL;
        }
        */

        /* save attr assignment */
        //attr_ref->attr_var = attr_var;
        //attr = attr_ref->attr;

        if (DEBUG_CLASS_RESOLVE_LEVEL_TMP) {
            knd_log("++ got attr: %.*s (id:%.*s)",
                    attr->name_size, attr->name,
                    attr->id_size, attr->id);
        }

        if (attr->is_a_set) {
            attr_var->attr = attr;

            err = resolve_attr_var_list(self, attr_var);
            if (err) return err;
            if (attr_var->val_size)
                attr_var->num_list_elems++;

            if (attr->is_indexed) {
                err = index_attr_var_list(self, attr, attr_var);
                if (err) return err;
            }
            continue;
        }

        /* single attr */
        switch (attr->type) {
        case KND_ATTR_AGGR:
            /* TODO */
            attr_var->attr = attr;
            err = resolve_aggr_item(self, attr_var);
            if (err) {
                knd_log("-- aggr attr_var not resolved :(");
                return err;
            }

            break;
        case KND_ATTR_REF:
            c = attr->ref_class;
            if (!c->is_resolved) {
                err = c->resolve(c, NULL);                                        RET_ERR();
            }
            err = resolve_class_ref(self, attr_var->val, attr_var->val_size,
                                    c, &attr_var->class);
            if (err) return err;
            break;
        case KND_ATTR_PROC:
            proc = attr->proc;
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

        /* TODO:
if (entry->attr->is_indexed) {
            err = index_attr(self, entry->attr, attr_var);
            if (err) return err;
            } */
        attr_var->attr = attr;
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_TMP) {
        knd_log("++ resolved attr vars of class %.*s!",
                self->entry->name_size, self->entry->name);
    }

    return knd_OK;
}

static int check_attr_name_conflict(struct kndClass *self,
                                    struct kndAttr *attr_candidate)
{
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    void *obj;
    struct kndRepo *repo = self->entry->repo;
    struct kndSet *attr_idx = self->attr_idx;
    struct ooDict *attr_name_idx = repo->attr_name_idx;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_TMP)
        knd_log(".. checking attrs name conflict: %.*s",
                attr_candidate->name_size,
                attr_candidate->name);

    /* global attr name search */
    attr_ref = attr_name_idx->get(attr_name_idx,
                                  attr_candidate->name, attr_candidate->name_size);
    if (!attr_ref) return knd_OK;

    while (attr_ref) {
        attr = attr_ref->attr;

        /* local attr storage lookup */
        err = attr_idx->get(attr_idx, attr->id, attr->id_size, &obj);
        if (!err) {
            knd_log("-- attr name collision detected: %.*s",
                    attr_candidate->name, attr_candidate->name_size);
            return knd_CONFLICT;
        }
        attr_ref = attr_ref->next;
    }

    return knd_OK;
}

static int register_new_attr(struct kndClass *self,
                             struct kndAttr *attr)
{
    struct kndRepo *repo = self->entry->repo;
    struct kndMemPool *mempool = repo->mempool;
    struct kndSet *attr_idx = repo->attr_idx;
    struct ooDict *attr_name_idx = repo->attr_name_idx;
    struct kndAttrRef *attr_ref, *prev_attr_ref;
    int err;

    repo->num_attrs++;
    attr->numid = repo->num_attrs;
    knd_num_to_str(attr->numid, attr->id, &attr->id_size, KND_RADIX_BASE);

    err = knd_attr_ref_new(mempool, &attr_ref);
    if (err) return err;
    attr_ref->attr = attr;

    /* global indices */
    prev_attr_ref = attr_name_idx->get(attr_name_idx,
                                       attr->name, attr->name_size);
    attr_ref->next = prev_attr_ref;
    err = attr_name_idx->set(attr_name_idx,
                             attr->name, attr->name_size,
                             (void*)attr_ref);                              RET_ERR();

    err = attr_idx->add(attr_idx,
                        attr->id, attr->id_size,
                        (void*)attr_ref);                                   RET_ERR();
    
    /* local index */
    err = self->attr_idx->add(self->attr_idx,
                              attr->id, attr->id_size,
                              (void*)attr_ref);                             RET_ERR();
    
    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log("++ new primary attr: \"%.*s\" numid:%zu",
                attr->name_size, attr->name, attr->numid);
    return knd_OK;
}

static int resolve_primary_attrs(struct kndClass *self)
{
    struct kndAttr *attr;
    struct kndClassEntry *entry;
    struct kndProc *root_proc;
    struct kndProcEntry *proc_entry;
    struct kndRepo *repo = self->entry->repo;
    struct ooDict *class_name_idx = repo->root_class->class_name_idx;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_TMP)
        knd_log(".. resolving primary attrs of %.*s.. [total:%zu]",
                self->name_size, self->name, self->num_attrs);

    for (attr = self->attrs; attr; attr = attr->next) {
        err = check_attr_name_conflict(self, attr);                      RET_ERR();
        
        /* computed attr idx */
        if (attr->proc) {
            self->computed_attrs[self->num_computed_attrs] = attr;
            self->num_computed_attrs++;
        }
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
            attr->ref_class = entry->class;
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
            if (DEBUG_CLASS_RESOLVE_LEVEL_2)
                knd_log("++ proc ref resolved: %.*s!",
                        proc_entry->name_size, proc_entry->name);
            break;
        default:
            break;
        }
        if (attr->is_implied)
            self->implied_attr = attr;

        /* no conflicts detected, register a new attr */
        err = register_new_attr(self, attr);                                      RET_ERR();
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
                self->inst_inbox_size);

    if (class_update) {
        class_update->insts = calloc(self->inst_inbox_size,
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

    for (obj = self->inst_inbox; obj; obj = obj->next) {
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
            if (class_update->num_insts >= self->inst_inbox_size) {
                knd_log("-- num objs mismatch in %.*s:  %zu vs %zu:(",
                        self->entry->name_size, self->entry->name,
                        class_update->num_insts, self->inst_inbox_size);
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

static int link_baseclass(struct kndClass *self,
                          struct kndClass *base)
{
    struct kndClassRef *ref, *baseref;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndSet *set;
    void *result;
    int err;
    /* register as a child */
    err = knd_class_ref_new(mempool, &ref);                                       RET_ERR();
    ref->class = self;
    ref->entry = self->entry;
    entry = base->entry;
    ref->next = entry->children;
    entry->children = ref;
    entry->num_children++;

    entry = self->entry;

    /* copy the ancestors */
    for (baseref = base->entry->ancestors; baseref; baseref = baseref->next) {
        set = baseref->entry->descendants;
        if (set) {
            err = set->get(set, entry->id, entry->id_size, &result);
            if (!err) {
                if (DEBUG_CLASS_RESOLVE_LEVEL_1)
                    knd_log("== link already established between %.*s and %.*s",
                            entry->name_size, entry->name,
                            baseref->entry->name_size, baseref->entry->name);
                continue;
            }
        }
        err = knd_class_ref_new(mempool, &ref);                                   RET_ERR();
        ref->class = baseref->class;
        ref->entry = baseref->entry;

        ref->next = entry->ancestors;
        entry->ancestors = ref;
        entry->num_ancestors++;
        if (!set) {
            err = mempool->new_set(mempool, &set);                                RET_ERR();
            set->type = KND_SET_CLASS;
            set->base = ref->entry;
            baseref->entry->descendants = set;
        }
        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            knd_log(".. add \"%.*s\" as a descendant of ancestor \"%.*s\"..",
                    entry->name_size, entry->name,
                    baseref->entry->name_size, baseref->entry->name);
        err = set->add(set, entry->id, entry->id_size,
                       (void*)entry);                                             RET_ERR();
    }
    /* register a parent */
    err = knd_class_ref_new(mempool, &ref);                                       RET_ERR();
    ref->class = base;
    ref->entry = base->entry;
    ref->next = entry->ancestors;
    entry->ancestors = ref;
    entry->num_ancestors++;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. add \"%.*s\" as a child of \"%.*s\"..",
                entry->name_size, entry->name,
                base->entry->name_size, base->entry->name);

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

    if (DEBUG_CLASS_RESOLVE_LEVEL_TMP)
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
            err = knd_get_class_by_id(self->entry->repo->root_class,
                                      cvar->id, cvar->id_size, &c);
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
            err = knd_get_class(self, classname, classname_size, &c);         RET_ERR();
        }

        if (DEBUG_CLASS_RESOLVE_LEVEL_TMP)
            c->str(c);

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
  
        err = knd_inherit_attrs(self, c);                            RET_ERR();
        err = link_baseclass(self, c);                           RET_ERR();

        cvar->entry->class = c;
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_TMP)
        knd_log("++ \"%.*s\" resolved its baseclasses!",
                self->name_size, self->name);
    return knd_OK;
}

extern int knd_resolve_class(struct kndClass *self,
                             struct kndClassUpdate *class_update)
{
    struct kndClass *root;
    struct kndClassVar *cvar;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndState *state;
    int err;

    if (self->is_resolved) {
        if (self->inst_inbox_size) {
            err = resolve_objs(self, class_update);                               RET_ERR();
        }

        if (self->attr_var_inbox_size) {
            err = knd_apply_attr_var_updates(self, class_update);                 RET_ERR();
        }
        return knd_OK;
    }

    self->entry->repo->num_classes++;
    entry = self->entry;
    entry->numid = self->entry->repo->num_classes;
    knd_num_to_str(entry->numid, entry->id, &entry->id_size, KND_RADIX_BASE);

    if (DEBUG_CLASS_RESOLVE_LEVEL_TMP) {
        knd_log(".. resolving class \"%.*s\" id:%.*s entry numid:%zu  batch mode:%d",
                self->entry->name_size, self->entry->name,
                entry->id_size, entry->id, self->entry->numid, self->batch_mode);
    }

    /* a child of the root class */
    if (!self->baseclass_vars) {
        err = link_baseclass(self, self->root_class);                             RET_ERR();

        self->is_resolved = true;
        return knd_OK;
    }

    err = resolve_baseclasses(self);                                              RET_ERR();

    for (cvar = self->baseclass_vars; cvar; cvar = cvar->next) {
        if (cvar->attrs) {
            err = resolve_attr_vars(self, cvar);                                  RET_ERR();
        }
    }

    /* primary attrs */
    if (self->num_attrs) {
        err = resolve_primary_attrs(self);                                        RET_ERR();
    }
    
    if (self->inst_inbox_size) {
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

    if (DEBUG_CLASS_RESOLVE_LEVEL_TMP)
        knd_log("++ class \"%.*s\" id:%.*s resolved!",
            self->entry->name_size, self->entry->name,
            self->entry->id_size, self->entry->id);

    self->is_resolved = true;
    return knd_OK;
}

extern int knd_resolve_classes(struct kndClass *self)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndSet *class_idx = self->entry->repo->root_class->class_idx;
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

        if (!c->is_resolved) {
            err = c->resolve(c, NULL);
            if (err) {
                knd_log("-- couldn't resolve the \"%.*s\" class :(",
                        c->entry->name_size, c->entry->name);
                return err;
            }
            c->is_resolved = true;
        }

        err = class_idx->add(class_idx,
                             c->entry->id, c->entry->id_size,
                             (void*)c->entry);
        if (err) return err;

        if (DEBUG_CLASS_RESOLVE_LEVEL_TMP)
            c->str(c);
    } while (key);

    return knd_OK;
}
