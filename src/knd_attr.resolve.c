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
#include "knd_dict.h"
#include "knd_shared_dict.h"
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

static int resolve_attr_var_list(struct kndClass *self,
                                 struct kndAttrVar *parent_item,
                                 struct kndTask *task);

static int str_attr_ref(void *obj,
                        const char *unused_var(elem_id),
                        size_t unused_var(elem_id_size),
                        size_t unused_var(count),
                        void *elem)
{
    struct kndClass *self = obj;
    struct kndAttrRef *ref = elem;
    knd_log("== attr: \"%.*s\" (class:%.*s)",
            ref->attr->name_size, ref->attr->name,
            self->name_size, self->name);
    return knd_OK;
}

static int resolve_text(struct kndAttrVar *attr_var, struct kndTask *task)
{
    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        knd_log(".. resolving text attr var: %.*s  class:%.*s",
                attr_var->name_size, attr_var->name,
                attr_var->class_var->parent->name_size,
                attr_var->class_var->parent->name);
    if (!attr_var->text) {
        KND_TASK_LOG("no text field (_t) found in attr var \"%.*s\"", attr_var->name_size, attr_var->name);
        return knd_FAIL;
    }
    return knd_OK;
}

static int resolve_implied_attr_var(struct kndClass *self,
                                    struct kndClass *c,
                                    const char *val, size_t val_size,
                                    struct kndAttrVar *var,
                                    struct kndTask *task)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndAttr *attr = c->implied_attr;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
        const char *attr_type_name = knd_attr_names[attr->type];
        size_t attr_type_name_size = strlen(attr_type_name);
        knd_log("== class: \"%.*s\" implied attr %.*s: \"%.*s\" check value:%.*s",
                c->name_size, c->name, attr_type_name_size, attr_type_name,
                attr->name_size, attr->name, val_size, val);
        knd_log(":: is list item:%d var attr:%.*s (set:%d)",
                var->is_list_item, var->attr->name_size, var->attr->name,
                var->attr->is_a_set);
    }
    var->implied_attr = attr;

    switch (attr->type) {
    case KND_ATTR_NUM:
        if (DEBUG_ATTR_RESOLVE_LEVEL_2)
            knd_log(".. resolving implied num attr: %.*s val:%.*s",
                    var->name_size, var->name,
                    var->val_size, var->val);
        if (var->val_size) {
            memcpy(buf, var->val, var->val_size);
            buf_size = var->val_size;
            buf[buf_size] = '\0';
            err = knd_parse_num(buf, &var->numval);
            // TODO: float parsing
        }
        break;
    case KND_ATTR_REF:
        err = knd_resolve_class_ref(self, val, val_size, attr->ref_class, &var->class, task);
        if (err) return err;
        break;
    case KND_ATTR_STR:
        //knd_log("++ STR value: %.*s", val_size, val);
        var->val = var->name;
        var->val_size = var->name_size;
        break;
    default:
        break;
    }
    return knd_OK;
}

static int resolve_inner_var(struct kndClass *self,
                             struct kndAttrVar *parent_item,
                             struct kndTask *task)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndClass *c;
    struct kndClassInst *ci;
    struct kndAttrVar *item;
    struct kndAttr *attr;
    struct kndAttrRef *attr_ref;
    struct kndProc *proc;
    const char *classname;
    size_t classname_size;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        knd_log(".. resolve inner var \"%.*s\" (blueprint:%.*s)",
                parent_item->name_size, parent_item->name,
                parent_item->attr->ref_classname_size, parent_item->attr->ref_classname);

    if (!parent_item->attr->ref_class) {
        err = knd_resolve_class_ref(self, parent_item->attr->ref_classname,
                                    parent_item->attr->ref_classname_size,
                                    NULL, &parent_item->attr->ref_class, task);
        if (err) return err;
    }
    c = parent_item->attr->ref_class;
    if (!c->is_resolved) {
        err = knd_class_resolve(c, task);                                         RET_ERR();
    }


    classname = parent_item->val;
    classname_size = parent_item->val_size;
    if (parent_item->is_list_item) {
        classname = parent_item->name;
        classname_size = parent_item->name_size;
    }

    if (parent_item->list) {
        err = resolve_attr_var_list(self, parent_item, task);
        if (err) return err;
        return knd_OK;
    }

    if (c->implied_attr) {
        if (DEBUG_ATTR_RESOLVE_LEVEL_2)
            knd_log("== class %.*s has implied attr \"%.*s\"",
                    c->name_size, c->name, c->implied_attr->name_size, c->implied_attr->name);

        err = resolve_implied_attr_var(self, c, classname, classname_size,
                                       parent_item, task);                        RET_ERR();
    }

    for (item = parent_item->children; item; item = item->next) {
        if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
            knd_log(".. check attr \"%.*s\" in class \"%.*s\" "
                    " is_resolved:%d",
                    item->name_size, item->name,
                    c->name_size, c->name, c->is_resolved);
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

        if (DEBUG_ATTR_RESOLVE_LEVEL_3)
            knd_log("++ got attr: %.*s (set:%d)", attr->name_size, attr->name, attr->is_a_set);

        if (attr->is_a_set) {
            err = resolve_attr_var_list(self, item, task);
            if (err) return err;
            continue;
        }

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
            err = resolve_inner_var(self, item, task);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = knd_resolve_class_ref(self, item->val, item->val_size,
                                        attr->ref_class, &item->class, task);
            if (err) return err;
            break;
        case KND_ATTR_REL:
            err = knd_get_class_inst(item->attr->ref_class->entry,
                                     item->val, item->val_size, task, &ci);
            if (err) return err;
            item->class_inst_entry = ci->entry;
            break;
        case KND_ATTR_PROC_REF:
            proc = attr->proc;
            err = knd_resolve_proc_ref(self, item->val, item->val_size,
                                       proc, &item->proc, task);
            if (err) return err;
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

static int resolve_attr_var_list(struct kndClass *self,
                                 struct kndAttrVar *var,
                                 struct kndTask *task)
{
    struct kndAttr *parent_attr = var->attr;
    struct kndAttrVar *item;
    struct kndClass *c, *local_class;
    const char *classname;
    size_t classname_size;
    int err;

    if (!var->list) {
        err = knd_FORMAT;
        KND_TASK_ERR("attr %.*s requires a list of items", parent_attr->name_size, parent_attr->name);
    }

    switch (parent_attr->type) {
        case KND_ATTR_STR:
            if (DEBUG_ATTR_RESOLVE_LEVEL_2)
                knd_log("NB: \"%.*s\" has ATTR_STR type, no resolving needed (list:%d  %p)",
                        var->name_size, var->name, var->is_list_item,
                        var->list);
            return knd_OK;
    default:
        break;
    }

    if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
        const char *attr_type_name = knd_attr_names[parent_attr->type];
        size_t attr_type_name_size = strlen(attr_type_name);
        knd_log("\n.. class \"%.*s\" to resolve attr item list \"%.*s\" "
                " of CLASS:%.*s   attr type:%.*s",
                self->entry->name_size, self->entry->name,
                var->name_size, var->name,
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
                    parent_attr->ref_classname, parent_attr->ref_classname_size);
            return err;
        }
    }

    /* base template class */
    c = parent_attr->ref_class;
    if (!c->is_resolved) {
        err = knd_class_resolve(c, task);                                         RET_ERR();
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
    if (var->val_size) {
        var->attr = parent_attr;
        switch (parent_attr->type) {
        case KND_ATTR_INNER:
            err = resolve_inner_var(self, var, task);
            if (err) {
                knd_log("-- first inner item not resolved :(");
                return err;
            }
            break;
        case KND_ATTR_REF:
            err = knd_resolve_class_ref(self, var->val, var->val_size, c, &var->class, task);
            if (err) return err;
            break;
        default:
            break;
        }
    }

    for (item = var->list; item; item = item->next) {
        item->attr = parent_attr;

        switch (parent_attr->type) {
        case KND_ATTR_INNER:
            err = resolve_inner_var(self, item, task);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            classname = item->name;
            classname_size = item->name_size;
            if (item->val_size) {
                classname = item->val;
                classname_size = item->val_size;
            }
            err = knd_resolve_class_ref(self, classname, classname_size, c, &item->class, task);
            if (err) return err;
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

static int register_new_attr(struct kndClass *self, struct kndAttr *attr, struct kndTask *task)
{
    struct kndRepo *repo =       self->entry->repo;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *attr_idx    = repo->attr_idx;
    struct kndAttrRef *attr_ref, *next_attr_ref;
    const char *name = attr->name;
    size_t name_size = attr->name_size;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        knd_log(".. register new attr: %.*s (host class: %.*s)",
                name_size, name, self->name_size, self->name);

    err = knd_attr_ref_new(mempool, &attr_ref);
    KND_TASK_ERR("failed to alloc kndAttrRef")
    attr_ref->attr = attr;
    attr_ref->class_entry = self->entry;

    /* generate unique attr id */
    attr->numid = atomic_fetch_add_explicit(&repo->attr_id_count, 1, memory_order_relaxed);
    attr->numid++;
    knd_uid_create(attr->numid, attr->id, &attr->id_size);

    if (task->type == KND_LOAD_STATE) {
        next_attr_ref = knd_shared_dict_get(repo->attr_name_idx, name, name_size);
        attr_ref->next = next_attr_ref;

        err = knd_shared_dict_set(repo->attr_name_idx, attr->name, attr->name_size,
                                  (void*)attr_ref, mempool, NULL, NULL, true);
        KND_TASK_ERR("failed to globally register attr name \"%.*s\"", name_size, name);

        err = attr_idx->add(attr_idx, attr->id, attr->id_size, (void*)attr_ref);
        KND_TASK_ERR("failed to globally register numid of attr \"%.*s\"", name_size, name);

        err = self->attr_idx->add(self->attr_idx, attr->id, attr->id_size, (void*)attr_ref);
        KND_TASK_ERR("failed to locally register numid of attr \"%.*s\"", name_size, name);
        return knd_OK;
    }

    /* local task name idx */
    err = knd_dict_set(task->attr_name_idx, name, name_size, (void*)attr_ref);
    KND_TASK_ERR("failed to register attr name %.*s", name_size, name);

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        knd_log("++ commit import: new primary attr registered: \"%.*s\" (id:%.*s)",
                name_size, name, attr->id_size, attr->id);

    return knd_OK;
}

static int check_attr_name_conflict(struct kndClass *self,
                                    struct kndAttr *attr_candidate,
                                    struct kndTask *task)
{
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    void *obj;
    struct kndRepo *repo = self->entry->repo;
    struct kndSet *attr_idx = self->attr_idx;
    struct kndSharedDict *attr_name_idx = repo->attr_name_idx;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        knd_log(".. checking attrs name conflict: %.*s",
                attr_candidate->name_size,
                attr_candidate->name);

    /* global attr name search */
    attr_ref = knd_shared_dict_get(attr_name_idx,
                                   attr_candidate->name, attr_candidate->name_size);
    if (!attr_ref) return knd_OK;

    while (attr_ref) {
        attr = attr_ref->attr;

        /* local attr storage lookup */
        err = attr_idx->get(attr_idx, attr->id, attr->id_size, &obj);
        if (!err) {
            err = knd_CONFLICT;
            KND_TASK_ERR("attr name %.*s already present in class %.*s",
                         attr_candidate->name_size, attr_candidate->name,
                         self->name_size, self->name);
        }
        attr_ref = attr_ref->next;
    }

    return knd_OK;
}

static int resolve_attr_ref(struct kndClass *self,
			    struct kndAttrVar *parent_item,
			    struct kndTask *task)
{
    struct kndRepo *repo = self->entry->repo;
    struct kndSharedDict *class_name_idx = repo->class_name_idx;
    const char *classname = NULL;
    size_t classname_size = 0;
    const char *attrname = NULL;
    size_t attrname_size = 0;
    const char *val_classname = NULL;
    size_t val_classname_size = 0;
    struct kndClassEntry *entry;
    struct kndAttrVar *attr_var = NULL;
    struct kndAttrRef *attr_ref;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
	knd_log(".. resolving attr ref %.*s..",
		parent_item->name_size, parent_item->name);
    }
    for (attr_var = parent_item->children; attr_var; attr_var = attr_var->next) {
	if (!memcmp(attr_var->name, "c", strlen("c"))) {
	    classname = attr_var->val;
	    classname_size = attr_var->val_size;
	}
	if (!memcmp(attr_var->name, "val", strlen("val"))) {
	    val_classname = attr_var->val;
	    val_classname_size = attr_var->val_size;
	}
    }
    if (!classname_size) {
        err = knd_FAIL;
	KND_TASK_ERR("no classname specified for attr ref \"%.*s\" (val:%.*s)",
                     parent_item->name_size, parent_item->name,
                     parent_item->val_size, parent_item->val);
    }

    entry = knd_shared_dict_get(class_name_idx, classname, classname_size);
    if (!entry) {
	 err = knd_NO_MATCH;
         KND_TASK_ERR("no such class: \"%.*s\" .."
                      "couldn't resolve the \"%.*s\" attr ref",
                      classname_size, classname,
                      parent_item->name_size, parent_item->name);
    }

    /* get attr name */
    attrname = parent_item->val;
    attrname_size = parent_item->val_size;
    if (!attrname_size) {
        err = knd_FAIL;
	KND_TASK_ERR("no attr name specified in attr ref \"%.*s\"",
                     parent_item->name_size, parent_item->name);
    }
    err = knd_class_get_attr(entry->class, attrname, attrname_size,
			     &attr_ref);
    if (err) {
	KND_TASK_ERR("no attr \"%.*s\" in class \"%.*s\"",
                     attrname_size, attrname,
                     entry->class->name_size, entry->class->name);
    }
    parent_item->class    = entry->class;
    parent_item->ref_attr = attr_ref->attr;

    if (val_classname_size) {
        entry = knd_shared_dict_get(class_name_idx, val_classname, val_classname_size);
        if (!entry) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("no such class: \"%.*s\" .."
                         "couldn't resolve the \"%.*s\" attr ref val",
                         val_classname_size, val_classname,
                         parent_item->name_size, parent_item->name);
        }
        parent_item->class_entry = entry;
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
    struct kndRepo *repo = self->entry->repo;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_3) {
        knd_log("\n.. resolving attr vars of class \"%.*s\" (base:%.*s) (repo:%.*s) ..",
                self->entry->name_size, self->entry->name,
                parent_item->entry->name_size, parent_item->entry->name,
                repo->name_size, repo->name);
    }

    for (attr_var = parent_item->attrs; attr_var; attr_var = attr_var->next) {
        if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
            knd_log(".. resolving attr var: %.*s",
                    attr_var->name_size, attr_var->name);
        }

        if (!memcmp("_proc", attr_var->name, attr_var->name_size)) {
            if (DEBUG_ATTR_RESOLVE_LEVEL_2) 
                knd_log(".. resolve _proc ref %.*s", attr_var->val_size, attr_var->val);
            continue;
        }

        err = knd_class_get_attr(self, attr_var->name, attr_var->name_size, &attr_ref);
        KND_TASK_ERR("no attr \"%.*s\" in class \"%.*s\"",
                     attr_var->name_size, attr_var->name, self->name_size, self->name);

        attr = attr_ref->attr;
        attr_ref->attr_var = attr_var;

        if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
            knd_log("\n.. resolving attr vars of class \"%.*s\" (repo:%.*s) ..",
                    self->entry->name_size, self->entry->name,
                    repo->name_size, repo->name);
            knd_log("++ got attr: %.*s [id:%.*s] indexed:%d",
                    attr->name_size, attr->name,
                    attr->id_size, attr->id, attr->is_indexed);
            knd_attr_var_str(attr_var, 1);
        }

        if (attr->is_a_set) {
            attr_var->attr = attr;

            err = resolve_attr_var_list(self, attr_var, task);
            KND_TASK_ERR("attr var list not resolved: %.*s", attr_var->name_size, attr_var->name);

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
            err = resolve_inner_var(self, attr_var, task);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            c = attr->ref_class;
            if (!c->is_resolved) {
                err = knd_class_resolve(c, task);
                KND_TASK_ERR("failed to resolve class %.*s", c->name_size, c->name);
            }
            err = knd_resolve_class_ref(self, attr_var->val, attr_var->val_size,
                                        c, &attr_var->class, task);
            if (err) return err;
            break;
        case KND_ATTR_TEXT:
            attr_var->attr = attr;
            err = resolve_text(attr_var, task);
            KND_TASK_ERR("failed to resolve text attr");
            break;
        case KND_ATTR_NUM:
            memcpy(buf, attr_var->val, attr_var->val_size);
            buf_size = attr_var->val_size;
            buf[buf_size] = '\0';
            err = knd_parse_num(buf, &attr_var->numval);
            break;
        case KND_ATTR_ATTR_REF:
            err = resolve_attr_ref(self, attr_var, task);
            if (err) return err;
            break;
        case KND_ATTR_PROC_REF:
            proc = attr->proc;
            err = knd_resolve_proc_ref(self, attr_var->val, attr_var->val_size,
                                       proc, &attr_var->proc, task);
            if (err) return err;
            break;
        default:
            /* atomic value, call a validation function? */
            break;
        }
        attr_var->attr = attr;
    }

    if (DEBUG_ATTR_RESOLVE_LEVEL_1) {
        knd_log("++ resolved attr vars of class \"%.*s\"!",
                self->entry->name_size, self->entry->name);
        self->attr_idx->map(self->attr_idx, str_attr_ref, (void*)self);
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
    struct kndSharedDict *class_name_idx = repo->class_name_idx;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        knd_log(".. resolving primary attrs of %.*s.. [total:%zu]",
                self->name_size, self->name, self->num_attrs);

    for (attr = self->attrs; attr; attr = attr->next) {
        err = check_attr_name_conflict(self, attr, task);
        KND_TASK_ERR("name conflict detected");

        switch (attr->type) {
        case KND_ATTR_INNER:
        case KND_ATTR_REF:
            if (!attr->ref_classname_size) {
                err = knd_FAIL;
                KND_TASK_ERR("no class specified for attr \"%s\"", attr->name);
            }
            entry = knd_shared_dict_get(class_name_idx,
                                        attr->ref_classname,
                                        attr->ref_classname_size);
            if (!entry) {
                err = knd_NO_MATCH;
                KND_TASK_ERR("no such class: \"%.*s\" .."
                             "couldn't resolve the \"%.*s\" attr of %.*s :(",
                             attr->ref_classname_size,
                             attr->ref_classname,
                             attr->name_size, attr->name,
                             self->entry->name_size, self->entry->name);
            }
            attr->ref_class = entry->class;
            break;
        case KND_ATTR_PROC_REF:
            if (!attr->ref_procname_size) {
                knd_log("-- no proc name specified for attr \"%s\"",
                        attr->name);
                return knd_FAIL;
            }
            proc_entry = knd_shared_dict_get(repo->proc_name_idx,
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
        err = register_new_attr(self, attr, task);
        KND_TASK_ERR("failed to register new attr");
    }
    return knd_OK;
}
