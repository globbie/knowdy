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
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_RESOLVE_LEVEL_1 0
#define DEBUG_ATTR_RESOLVE_LEVEL_2 0
#define DEBUG_ATTR_RESOLVE_LEVEL_3 0
#define DEBUG_ATTR_RESOLVE_LEVEL_4 0
#define DEBUG_ATTR_RESOLVE_LEVEL_5 0
#define DEBUG_ATTR_RESOLVE_LEVEL_TMP 1

static int resolve_text(struct kndAttrVar *attr_var,
                        struct kndTask *task)
{
    struct kndAttrVar *curr_item;
    struct kndAttrVar *val_item;
    struct kndText *text;
    struct kndTranslation *tr;
    struct kndMemPool *mempool = task->mempool;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
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

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        knd_log(".. resolve inner item %.*s (val:%.*s) is list item:%d..",
                parent_item->name_size, parent_item->name,
                parent_item->val_size, parent_item->val,
                parent_item->is_list_item);

    if (!parent_item->attr->ref_class) {
        err = knd_resolve_class_ref(self,
                                    parent_item->attr->ref_classname,
                                    parent_item->attr->ref_classname_size,
                                    NULL, &parent_item->attr->ref_class,
                                    task);
        if (err) return err;
    }
    c = parent_item->attr->ref_class;
    if (!c->is_resolved) {
        err = knd_class_resolve(c, task);                                                RET_ERR();
    }

    if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
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

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        c->str(c, 1);

    if (c->implied_attr) {
        attr = c->implied_attr;

        if (DEBUG_ATTR_RESOLVE_LEVEL_2)
            knd_log("== class: \"%.*s\" implied attr: %.*s",
                    classname_size, classname,
                    attr->name_size, attr->name);

        parent_item->implied_attr = attr;

        switch (attr->type) {
        case KND_ATTR_NUM:

            if (DEBUG_ATTR_RESOLVE_LEVEL_2)
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
            err = knd_resolve_class_ref(self,
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

        if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
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
            if (DEBUG_ATTR_RESOLVE_LEVEL_2)
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
            if (DEBUG_ATTR_RESOLVE_LEVEL_2)
                knd_log("== nested inner item found: %.*s",
                        item->name_size, item->name);
            err = resolve_inner_item(self, item, task);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            classname = item->val;
            classname_size = item->val_size;
            err = knd_resolve_class_ref(self,
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

    if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
        const char *attr_type_name = knd_attr_names[parent_attr->type];
        size_t attr_type_name_size = strlen(attr_type_name);
        knd_log("\n.. class %.*s to resolve attr item list \"%.*s\" "
                " of CLASS:%.*s   attr type:%.*s",
                self->entry->name_size, self->entry->name,
                parent_item->name_size, parent_item->name,
                parent_attr->ref_classname_size,
                parent_attr->ref_classname,
                attr_type_name_size, attr_type_name);
    }

    /* resolve template class ref */
    if (!parent_attr->ref_class) {
        err = knd_resolve_class_ref(self,
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

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        c->str(c, 1);

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
            err = knd_resolve_class_ref(self,
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

        switch (parent_attr->type) {
        case KND_ATTR_INNER:
            err = resolve_inner_item(self, item, task);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            if (item->val_size) {
                err = knd_resolve_class_ref(self, item->val, item->val_size,
                                        c, &item->class, task);
                if (err) {
                    knd_log("-- class ref not resolved");
                    return err;
                }
            } else {
                err = knd_resolve_class_ref(self,
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

static int register_new_attr(struct kndClass *self,
                             struct kndAttr *attr,
                             struct kndTask *task)
{
    struct kndRepo *repo =       self->entry->repo;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *attr_idx    = repo->attr_idx;
    struct kndDict *attr_name_idx = task->ctx->attr_name_idx;
    struct kndAttrRef *attr_ref, *prev_attr_ref;
    int err;

    /* generate unique attr id */
    attr->numid = atomic_fetch_add_explicit(&repo->attr_id_count, 1,
                                            memory_order_relaxed);
    attr->numid++;
    knd_uid_create(attr->numid, attr->id, &attr->id_size);

    err = knd_attr_ref_new(mempool, &attr_ref);
    if (err) {
        return err;
    }
    attr_ref->attr = attr;
    attr_ref->class_entry = self->entry;

    /* global indices */
    prev_attr_ref = knd_dict_get(attr_name_idx,
                                 attr->name, attr->name_size);
    if (prev_attr_ref) {
        attr_ref->next = prev_attr_ref->next;
        prev_attr_ref->next = attr_ref;
    } else {
        err = knd_dict_set(attr_name_idx,
                           attr->name, attr->name_size,
                           (void*)attr_ref);                                      RET_ERR();
    }

    err = attr_idx->add(attr_idx,
                        attr->id, attr->id_size,
                        (void*)attr_ref);                                         RET_ERR();

    /* local class index */
    err = self->attr_idx->add(self->attr_idx,
                              attr->id, attr->id_size,
                              (void*)attr_ref);                                   RET_ERR();

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        knd_log("++ new primary attr registered: \"%.*s\" (id:%.*s)",
                attr->name_size, attr->name, attr->id_size, attr->id);

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
    struct kndDict *attr_name_idx = repo->attr_name_idx;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        knd_log(".. checking attrs name conflict: %.*s",
                attr_candidate->name_size,
                attr_candidate->name);

    /* global attr name search */
    attr_ref = knd_dict_get(attr_name_idx,
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

int knd_resolve_attr_vars(struct kndClass *self,
                          struct kndClassVar *parent_item,
                          struct kndTask *task)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndAttrVar *attr_var;
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    struct kndClass *c;
    struct kndProc *proc;
    struct kndSet *attr_idx = self->attr_idx;
    struct kndRepo *repo = self->entry->repo;
    struct kndOutput *log = task->log;
    void *obj;
    int e, err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
        knd_log("\n.. resolving attr vars of class \"%.*s\" (repo:%.*s) ..",
                self->entry->name_size, self->entry->name,
                repo->name_size, repo->name);
    }

    for (attr_var = parent_item->attrs; attr_var; attr_var = attr_var->next) {
        if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
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
            if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
                knd_log("++ set attr var: %.*s val:%.*s",
                        attr->name_size, attr->name,
                        attr_var->val_size, attr_var->val);
            }
            attr_ref = obj;
            attr_ref->attr_var = attr_var;
        }

        if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
            knd_log("\n.. resolving attr vars of class \"%.*s\" (repo:%.*s) ..",
                    self->entry->name_size, self->entry->name,
                    repo->name_size, repo->name);
            knd_log("++ got attr: %.*s [id:%.*s] indexed:%d",
                    attr->name_size, attr->name,
                    attr->id_size, attr->id, attr->is_indexed);
            str_attr_vars(attr_var, 1);
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

            /*if (attr->is_indexed) {
                err = knd_index_attr_var_list(self, attr, attr_var, task);
                if (err) return err;
                }*/

            /*task->attr = attr;
            switch (attr->type) {
            case KND_ATTR_INNER:
                for (item = attr_var->list; item; item = item->next) {
                    err = knd_index_inner_attr_var(self, item, task);             RET_ERR();
                }
              break;
            default:
                break;
            }
            */
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
            err = knd_resolve_class_ref(self, attr_var->val, attr_var->val_size,
                                        c, &attr_var->class, task);
            if (err) return err;
            break;
        case KND_ATTR_TEXT:
            err = resolve_text(attr_var, task);                                    RET_ERR();
            break;
        case KND_ATTR_NUM:
            memcpy(buf, attr_var->val, attr_var->val_size);
            buf_size = attr_var->val_size;
            buf[buf_size] = '\0';
            err = knd_parse_num(buf, &attr_var->numval);
            break;
        case KND_ATTR_PROC_REF:
            proc = attr->proc;
            /*if (!c->is_resolved) {
                err = knd_class_resolve(c);                                        RET_ERR();
                }*/
            err = knd_resolve_proc_ref(self, attr_var->val, attr_var->val_size,
                                       proc, &attr_var->proc, task);
            if (err) return err;
            break;
        default:
            /* atomic value, call a validation function? */
            break;
        }

        /*if (attr->is_indexed) {
            err = knd_index_attr(self, attr, attr_var, task);
            if (err) return err;
            }*/
        attr_var->attr = attr;
    }

    if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
        knd_log("++ resolved attr vars of class %.*s!",
                self->entry->name_size, self->entry->name);
    }
    return knd_OK;
}

int knd_resolve_primary_attrs(struct kndClass *self,
                              struct kndTask *task)
{
    struct kndAttr *attr;
    struct kndClassEntry *entry;
    struct kndProcEntry *proc_entry;
    struct kndRepo *repo = self->entry->repo;
    struct kndDict *class_name_idx = repo->class_name_idx;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
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
            entry = knd_dict_get(class_name_idx,
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
        case KND_ATTR_PROC_REF:
            if (!attr->ref_procname_size) {
                knd_log("-- no proc name specified for attr \"%s\"",
                        attr->name);
                return knd_FAIL;
            }
            proc_entry = knd_dict_get(repo->proc_name_idx,
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
            if (DEBUG_ATTR_RESOLVE_LEVEL_2)
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
