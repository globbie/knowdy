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

extern int knd_inherit_attrs(struct kndClass *self, struct kndClass *base,
                             struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    int err;

    if (!base->is_resolved) {
        err = knd_class_resolve(base, task);                                          RET_ERR();
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log(".. \"%.*s\" class to inherit attrs from \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                base->name_size, base->name);
    }

    /* copy your parent's attr idx */
    self->attr_idx->mempool = mempool;
    err = base->attr_idx->map(base->attr_idx,
                              knd_copy_attr_ref,
                              (void*)self->attr_idx);                             RET_ERR();

    return knd_OK;
}

static int index_attr(struct kndClass *self,
                      struct kndAttr *attr,
                      struct kndAttrVar *item,
                      struct kndTask *task)
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
    err = knd_get_class(self->entry->repo,
                        attr->ref_classname,
                        attr->ref_classname_size,
                        &base, task);                                                       RET_ERR();
    if (!base->is_resolved) {
        err = knd_class_resolve(base, task);                                          RET_ERR();
    }

    /* specific class */
    err = knd_get_class(self->entry->repo,
                        item->val,
                        item->val_size, &c, task);                                          RET_ERR();

    item->class = c;

    if (!c->is_resolved) {
        err = knd_class_resolve(c, task);                                                RET_ERR();
    }

    err = knd_is_base(base, c);                                                   RET_ERR();

    set = attr->parent_class->entry->descendants;

    /* add curr class to the reverse index */
    err = knd_set_add_ref(set, attr, self->entry, c->entry);
    if (err) return err;

    return knd_OK;
}

static int index_attr_var_list(struct kndClass *self,
                               struct kndAttr *attr,
                               struct kndAttrVar *parent_item,
                               struct kndTask *task)
{
    struct kndClass *base;
    struct kndSet *set;
    struct kndClass *c, *idx_class, *curr_class;
    struct kndClassRef *ref;
    struct kndAttrVar *item = parent_item;
    struct kndMemPool *mempool = task->mempool;
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
    err = knd_get_class(self->entry->repo,
                        attr->ref_classname,
                        attr->ref_classname_size,
                        &base, task);                                                       RET_ERR();
    if (!base->is_resolved) {
        err = knd_class_resolve(base, task);                                          RET_ERR();
    }

    for (item = parent_item->list; item; item = item->next) {
        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            knd_log("== index list item:%.*s", item->name_size, item->name);

        /* specific class */
        err = knd_get_class(self->entry->repo,
                            item->name,
                            item->name_size, &c, task);                                 RET_ERR();
        item->class = c;

        if (!c->is_resolved) {
            err = knd_class_resolve(c, task);                                           RET_ERR();
        }

        err = knd_is_base(base, c);
        if (err) {
            knd_log("-- no inheritance from \"%.*s\" to \"%.*s\"",
                    base->name_size, base->name, c->name_size, c->name);
            return err;
        }

        idx_class = attr->parent_class;

        if (idx_class->entry->repo != self->entry->repo) {
            for (ref = self->entry->ancestors; ref; ref = ref->next) {
                curr_class = ref->class;
                if (curr_class->entry->orig == idx_class->entry) {
                    idx_class = curr_class;
                }
            }

        }

        set = idx_class->entry->descendants;
        if (!set) {
            err = knd_set_new(mempool, &set);                                     RET_ERR();
            set->type = KND_SET_CLASS;
            set->base =  idx_class->entry;
            idx_class->entry->descendants = set;
        }

        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
            knd_log("\n.. add %.*s ref to %.*s (repo:%.*s)",
                    item->name_size, item->name,
                    idx_class->name_size,
                    idx_class->name,
                    idx_class->entry->repo->name_size,
                    idx_class->entry->repo->name);

        /* add curr class to the reverse index */
        err = knd_set_add_ref(set, attr, self->entry, c->entry);
        if (err) return err;
    }
    return knd_OK;
}

static int resolve_text(struct kndAttrVar *attr_var,
                        struct kndTask *task)
{
    struct kndAttrVar *curr_item;
    struct kndAttrVar *val_item;
    struct kndText *text;
    struct kndTranslation *tr;
    struct kndMemPool *mempool = task->mempool;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. resolving text attr var: %.*s  class:%.*s",
                attr_var->name_size, attr_var->name,
                attr_var->class_var->parent->name_size,
                attr_var->class_var->parent->name);

    //str_attr_vars(attr_var, 1);

    err = knd_text_new(mempool, &text);
    if (err) {
        knd_log("-- no text alloc");
        return err;
    }

    for (curr_item = attr_var->list; curr_item; curr_item = curr_item->next) {
        err = knd_text_translation_new(mempool, &tr);
        if (err) {
            knd_log("-- no text alloc");
        }

        tr->locale = curr_item->name;
        tr->locale_size = curr_item->name_size;

        // no text value
        if (!curr_item->children) {
            knd_log("-- no children");
            return knd_FAIL;
        }
        val_item = curr_item->children;
        if (memcmp(val_item->name, "t", 1)) {
            knd_log("-- no t field");
            return knd_FAIL;
        }

        tr->val = val_item->val;
        tr->val_size = val_item->val_size;

        tr->next = text->tr;
        text->tr = tr;
        //str_attr_vars(curr_item, 1);
    }

    attr_var->text = text;

    return knd_OK;
}

static int resolve_class_ref(struct kndClass *self,
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

static int resolve_proc_ref(struct kndClass *self,
                            const char *name, size_t name_size,
                            struct kndProc *unused_var(base),
                            struct kndProc **result,
                            struct kndTask *unused_var(task))
{
    struct kndProc *proc;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. resolving proc ref:  %.*s", name_size, name);

    err = knd_get_proc(self->entry->repo,
                       name, name_size, &proc);                            RET_ERR();

    /*c = dir->conc;
    if (!c->is_resolved) {
        err = knd_class_resolve(c);                                                RET_ERR();
    }

    if (base) {
        err = is_base(base, c);                                                   RET_ERR();
    }
    */

    *result = proc;

    return knd_OK;
}

static int resolve_inner_item(struct kndClass *self,
                              struct kndAttrVar *parent_item,
                             struct kndTask *task)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndClass *c;
    struct kndAttrVar *item;
    struct kndAttr *attr;
    struct kndAttrRef *attr_ref;
    const char *classname;
    size_t classname_size;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. resolve inner item %.*s..",
                parent_item->name_size, parent_item->name);

    if (!parent_item->attr->ref_class) {
        err = resolve_class_ref(self,
                                parent_item->attr->ref_classname,
                                parent_item->attr->ref_classname_size,
                                NULL, &parent_item->attr->ref_class, task);
        if (err) return err;
    }
    c = parent_item->attr->ref_class;
    if (!c->is_resolved) {
        err = knd_class_resolve(c, task);                                                RET_ERR();
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log("\n.. resolving inner item \"%.*s\""
                " class:%.*s [resolved:%d]] is_list_item:%d",
                parent_item->name_size,  parent_item->name,
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
        case KND_ATTR_INNER:
            break;
        case KND_ATTR_REF:
            err = resolve_class_ref(self,
                                    classname, classname_size,
                                    attr->ref_class, &parent_item->class, task);
            if (err) return err;
            break;
        default:
            break;
        }
    }

    /* resolve nested children */
    for (item = parent_item->children; item; item = item->next) {
        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
            knd_log(".. check attr \"%.*s\" in class \"%.*s\" (repo:%.*s) "
                    " is_resolved:%d",
                    item->name_size, item->name,
                    c->name_size, c->name,
                     c->entry->repo->name_size, c->entry->repo->name, c->is_resolved);
        }
        err = knd_class_get_attr(c, item->name, item->name_size, &attr_ref);
        if (err) {
            knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                    item->name_size, item->name,
                    c->name_size, c->name);
            return err;
        }
        attr = attr_ref->attr;
        item->attr = attr;
        switch (attr->type) {
        case KND_ATTR_NUM:
            if (DEBUG_CLASS_RESOLVE_LEVEL_2)
                knd_log(".. resolving default num attr: %.*s val:\"%.*s\" val size:%zu",
                        item->name_size, item->name,
                        item->val_size, item->val, item->val_size);

            // FIX
            memcpy(buf, item->val, item->val_size);
            buf_size = item->val_size;
            buf[buf_size] = '\0';
            err = knd_parse_num(buf, &item->numval);
            break;
        case KND_ATTR_INNER:
            if (DEBUG_CLASS_RESOLVE_LEVEL_2)
                knd_log("== nested inner item found: %.*s",
                        item->name_size, item->name);
            err = resolve_inner_item(self, item, task);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            classname = item->val;
            classname_size = item->val_size;
            err = resolve_class_ref(self,
                                    classname, classname_size,
                                    attr->ref_class, &item->class, task);
            if (err) return err;
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

static int resolve_attr_var_list(struct kndClass *self,
                                 struct kndAttrVar *parent_item,
                                 struct kndTask *task)
{
    struct kndAttr *parent_attr = parent_item->attr;
    struct kndAttrVar *item;
    struct kndClass *c, *local_class;
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

    /* resolve template class ref */
    if (!parent_attr->ref_class) {
        err = resolve_class_ref(self,
                                parent_attr->ref_classname,
                                parent_attr->ref_classname_size,
                                NULL, &parent_attr->ref_class, task);
        if (err) {
            knd_log("-- ref not resolved: :%.*s",
                    parent_attr->ref_classname,
                    parent_attr->ref_classname_size);
            return err;
        }
    }

    /* base template class */
    c = parent_attr->ref_class;
    if (!c->is_resolved) {
        err = knd_class_resolve(c, task);                                                RET_ERR();
    }

    /* does local repo have a clone of this class? */
    if (self->entry->repo != c->entry->repo) {
        err = knd_get_class(self->entry->repo,
                            parent_attr->ref_classname,
                            parent_attr->ref_classname_size,
                            &local_class, task);
        if (!err)
            c = local_class;
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        c->str(c);

    /* first item */
    if (parent_item->val_size) {
        parent_item->attr = parent_attr;

        switch (parent_attr->type) {
        case KND_ATTR_INNER:
            err = resolve_inner_item(self, parent_item, task);
            if (err) {
                knd_log("-- first inner item not resolved :(");
                return err;
            }
            break;
        case KND_ATTR_REF:
            err = resolve_class_ref(self,
                                    parent_item->val, parent_item->val_size,
                                    c, &parent_item->class, task);
            if (err) return err;
            break;
        default:
            break;
        }
    }

    for (item = parent_item->list; item; item = item->next) {
        item->attr = parent_attr;

        //knd_log("== list item:%.*s", item->name_size, item->name);

        switch (parent_attr->type) {
        case KND_ATTR_INNER:
            err = resolve_inner_item(self, item, task);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            if (item->val_size) {
                err = resolve_class_ref(self, item->val, item->val_size,
                                        c, &item->class, task);
                if (err) {
                    knd_log("-- class ref not resolved");
                    return err;
                }
            } else {
                err = resolve_class_ref(self,
                                        item->name, item->name_size,
                                        c, &item->class, task);
                if (err) {
                    knd_log("-- class ref not resolved :(");
                    return err;
                }
            }

            break;
        default:
            break;
        }
    }
    return knd_OK;
}

static int resolve_attr_vars(struct kndClass *self,
                             struct kndClassVar *parent_item,
                             struct kndTask *task)
{
    struct kndAttrVar *attr_var;
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    struct kndClass *c;
    struct kndProc *proc;
    struct kndSet *attr_idx = self->attr_idx;
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *log = task->log;
    void *obj;
    int e, err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log("\n.. resolving attr vars of class \"%.*s\" (repo:%.*s) ..",
                self->entry->name_size, self->entry->name,
                repo->name_size, repo->name);
    }

    for (attr_var = parent_item->attrs; attr_var; attr_var = attr_var->next) {
        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
            knd_log(".. resolving attr var: %.*s",
                    attr_var->name_size, attr_var->name);
        }
        err = knd_class_get_attr(self,
                                 attr_var->name, attr_var->name_size,
                                 &attr_ref);
        if (err) {
            knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                    attr_var->name_size, attr_var->name,
                    self->name_size, self->name);
            log->reset(log);
            e = log->write(log, attr_var->name, attr_var->name_size);
            if (e) return e;
            e = log->write(log, ": no such attribute",
                           strlen(": no such attribute"));
            if (e) return e;
            task->http_code = HTTP_NOT_FOUND;
            return knd_FAIL;
        }
        attr = attr_ref->attr;

        err = attr_idx->get(attr_idx, attr->id, attr->id_size, &obj);
        if (!err) {
            if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
                knd_log("++ set attr var: %.*s val:%.*s",
                        attr->name_size, attr->name,
                        attr_var->val_size, attr_var->val);
            }
            attr_ref = obj;
            attr_ref->attr_var = attr_var;
        }

        if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
            knd_log("++ got attr: %.*s [id:%.*s]",
                    attr->name_size, attr->name,
                    attr->id_size, attr->id);
        }

        if (attr->is_a_set) {
            attr_var->attr = attr;

            err = resolve_attr_var_list(self, attr_var, task);
            if (err) {
                knd_log("-- attr var list not resolved: %.*s",
                        attr_var->name_size, attr_var->name);
                return err;
            }
            if (attr_var->val_size)
                attr_var->num_list_elems++;

            if (attr->is_indexed) {
                err = index_attr_var_list(self, attr, attr_var, task);
                if (err) return err;
            }
            continue;
        }

        /* single attr */
        switch (attr->type) {
        case KND_ATTR_INNER:
            /* TODO */
            attr_var->attr = attr;
            err = resolve_inner_item(self, attr_var, task);
            if (err) {
                knd_log("-- inner attr_var not resolved :(");
                return err;
            }

            break;
        case KND_ATTR_REF:
            c = attr->ref_class;
            if (!c->is_resolved) {
                err = knd_class_resolve(c, task);                                        RET_ERR();
            }
            err = resolve_class_ref(self, attr_var->val, attr_var->val_size,
                                    c, &attr_var->class, task);
            if (err) return err;
            break;
        case KND_ATTR_TEXT:
            err = resolve_text(attr_var, task);                                    RET_ERR();
            break;
        case KND_ATTR_PROC:
            proc = attr->proc;
            /*if (!c->is_resolved) {
                err = knd_class_resolve(c);                                        RET_ERR();
                }*/
            err = resolve_proc_ref(self, attr_var->val, attr_var->val_size,
                                   proc, &attr_var->proc, task);
            if (err) return err;
            break;
        default:
            /* atomic value, call a validation function? */
            break;
        }

        if (attr->is_indexed) {
            err = index_attr(self, attr, attr_var, task);
            if (err) return err;
        }
        attr_var->attr = attr;
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
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

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
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
                    attr_candidate->name_size, attr_candidate->name);
            return knd_CONFLICT;
        }
        attr_ref = attr_ref->next;
    }

    return knd_OK;
}

static int register_new_attr(struct kndClass *self,
                             struct kndAttr *attr,
                             struct kndTask *task)
{
    struct kndRepo *repo = self->entry->repo;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *attr_idx = repo->attr_idx;
    struct ooDict *attr_name_idx = repo->attr_name_idx;
    struct kndAttrRef *attr_ref, *prev_attr_ref;
    int err;

    repo->num_attrs++;
    attr->numid = repo->num_attrs;
    knd_uid_create(attr->numid, attr->id, &attr->id_size);

    err = knd_attr_ref_new(mempool, &attr_ref);
    if (err) return err;
    attr_ref->attr = attr;
    attr_ref->class_entry = self->entry;

    /* global indices */
    prev_attr_ref = attr_name_idx->get(attr_name_idx,
                                       attr->name, attr->name_size);
    attr_ref->next = prev_attr_ref;

    if (prev_attr_ref) {
        err = attr_name_idx->remove(attr_name_idx,
                                    attr->name, attr->name_size);           RET_ERR();
    }

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

static int resolve_primary_attrs(struct kndClass *self,
                                 struct kndTask *task)
{
    struct kndAttr *attr;
    struct kndClassEntry *entry;
    struct kndProcEntry *proc_entry;
    struct kndRepo *repo = self->entry->repo;
    struct ooDict *class_name_idx = repo->class_name_idx;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. resolving primary attrs of %.*s.. [total:%zu]",
                self->name_size, self->name, self->num_attrs);

    for (attr = self->attrs; attr; attr = attr->next) {
        err = check_attr_name_conflict(self, attr);                      RET_ERR();
        
        switch (attr->type) {
        case KND_ATTR_INNER:
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
            proc_entry = repo->proc_name_idx->get(repo->proc_name_idx,
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
        err = register_new_attr(self, attr, task);                                      RET_ERR();
    }
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
        err = knd_class_clone(base, repo, &base_copy, mempool);               RET_ERR();
        base = base_copy;
        err = link_ancestor(self, base->entry, task);                                   RET_ERR();
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
        err = link_ancestor(self, baseref->entry, task);                                RET_ERR();
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
    }

    return knd_OK;
}

static int resolve_baseclasses(struct kndClass *self,
                               struct kndTask *task)
{
    struct kndClassVar *cvar;
    struct kndClassEntry *entry;
    struct kndClass *c = NULL;
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
            err = knd_get_class_by_id(self->entry->repo->root_class,
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
                return err;
            }
        }

        if (DEBUG_CLASS_RESOLVE_LEVEL_2)
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
  
        err = knd_inherit_attrs(self, c, task);                            RET_ERR();

        err = link_baseclass(self, c, task);                               RET_ERR();
        cvar->entry->class = c;
    }

    if (DEBUG_CLASS_RESOLVE_LEVEL_2) {
        knd_log("++ \"%.*s\" has resolved its baseclasses!",
                self->name_size, self->name);
        self->str(self);
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
                err = resolve_attr_vars(self, cvar, task);                        RET_ERR();
            }
        }
    }

    /* primary attrs */
    if (self->num_attrs) {
        err = resolve_primary_attrs(self, task);                                  RET_ERR();
    }
    
    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log("++ class \"%.*s\" id:%.*s resolved!",
            self->entry->name_size, self->entry->name,
            self->entry->id_size, self->entry->id);

    self->is_resolved = true;
    return knd_OK;
}

extern int knd_resolve_classes(struct kndClass *self,
                               struct kndTask *task)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndSet *class_idx = self->entry->repo->class_idx;
    struct ooDict *class_name_idx = self->entry->repo->class_name_idx;
    const char *key;
    void *val;
    int err;

    if (DEBUG_CLASS_RESOLVE_LEVEL_2)
        knd_log(".. resolving classes in \"%.*s\"",
                self->entry->name_size, self->entry->name);

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
                c->str(c);
        }

    } while (key);

    return knd_OK;
}
