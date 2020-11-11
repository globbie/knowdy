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

#define DEBUG_ATTR_VAR_RESOLVE_LEVEL_1 0
#define DEBUG_ATTR_VAR_RESOLVE_LEVEL_2 0
#define DEBUG_ATTR_VAR_RESOLVE_LEVEL_3 0
#define DEBUG_ATTR_VAR_RESOLVE_LEVEL_4 0
#define DEBUG_ATTR_VAR_RESOLVE_LEVEL_5 0
#define DEBUG_ATTR_VAR_RESOLVE_LEVEL_TMP 1

static int resolve_attr_var_list(struct kndClass *self, struct kndAttrVar *parent_item, struct kndTask *task);

static int resolve_text(struct kndAttrVar *attr_var, struct kndTask *task)
{
    if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2)
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

static int resolve_implied_attr_var(struct kndClass *self, struct kndClass *c, const char *val, size_t val_size,
                                    struct kndAttrVar *var, struct kndTask *task)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndAttr *attr = c->implied_attr;
    int err;

    if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2) {
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
        if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2)
            knd_log(".. resolving implied num attr: %.*s val:%.*s",
                    var->name_size, var->name, var->val_size, var->val);
        if (var->val_size) {
            memcpy(buf, var->val, var->val_size);
            buf_size = var->val_size;
            buf[buf_size] = '\0';
            err = knd_parse_num(buf, &var->numval);
            // TODO: float parsing
        }
        break;
    case KND_ATTR_REF:
        err = knd_resolve_class_ref(self, val, val_size, attr->ref_class_entry->class, &var->class, task);
        KND_TASK_ERR("failed to resolve class ref %.*s", val_size, val);
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

static int resolve_inner_var(struct kndClass *self, struct kndAttrVar *var, struct kndTask *task)
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

    if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2)
        knd_log(".. resolve inner var \"%.*s\" (blueprint:%.*s)",
                var->name_size, var->name,
                var->attr->ref_classname_size, var->attr->ref_classname);

    if (!var->attr->ref_class_entry) {
        err = knd_resolve_class_ref(self, var->attr->ref_classname,
                                    var->attr->ref_classname_size,
                                    NULL, &c, task);
        if (err) return err;
        var->attr->ref_class_entry = c->entry;
    }

    c = var->attr->ref_class_entry->class;
    // TODO if (!c)

    if (!c->is_resolved) {
        err = knd_class_resolve(c, task);
        KND_TASK_ERR("failed to resolve class %.*s", c->name_size, c->name);
    }
    classname = var->val;
    classname_size = var->val_size;
    if (var->is_list_item) {
        classname = var->name;
        classname_size = var->name_size;
    }

    if (var->list) {
        err = resolve_attr_var_list(self, var, task);
        if (err) return err;
        return knd_OK;
    }

    if (c->implied_attr) {
        if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2)
            knd_log("== class %.*s has implied attr \"%.*s\" var:%.*s (class:%.*s)",
                    c->name_size, c->name, c->implied_attr->name_size, c->implied_attr->name,
                    var->name_size, var->name, classname_size, classname);

        err = resolve_implied_attr_var(self, c, classname, classname_size, var, task);
        KND_TASK_ERR("failed to resolve implied attr var");
    }

    FOREACH (item, var->children) {
        if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2) {
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

        if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_3)
            knd_log("++ got attr: %.*s (set:%d)", attr->name_size, attr->name, attr->is_a_set);

        if (attr->is_a_set) {
            err = resolve_attr_var_list(self, item, task);
            if (err) return err;
            continue;
        }

        switch (attr->type) {
        case KND_ATTR_NUM:
            if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2)
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
                                        attr->ref_class_entry->class, &item->class, task);
            if (err) return err;
            break;
        case KND_ATTR_REL:
            err = knd_get_class_inst(item->attr->ref_class_entry->class, item->val, item->val_size, task, &ci);
            if (err) return err;
            item->class_inst_entry = ci->entry;
            break;
        case KND_ATTR_TEXT:
            item->attr = attr;
            err = resolve_text(item, task);
            KND_TASK_ERR("failed to resolve text attr");
            break;
        case KND_ATTR_PROC_REF:
            proc = attr->proc;
            err = knd_resolve_proc_ref(self, item->val, item->val_size, proc, &item->proc, task);
            if (err) return err;
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

static int resolve_attr_var_list(struct kndClass *self, struct kndAttrVar *var, struct kndTask *task)
{
    struct kndAttr *attr = var->attr;
    struct kndAttrVar *item;
    struct kndClassEntry *entry;
    struct kndClass *c, *local_class;
    const char *classname;
    size_t classname_size;
    int err;

    assert(var->list != NULL);
        /*if (!var->list) {
        err = knd_FORMAT;
        KND_TASK_ERR("attr %.*s requires a list of items", attr->name_size, attr->name);
        }*/

    switch (attr->type) {
        case KND_ATTR_STR:
            if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2)
                knd_log("NB: \"%.*s\" has ATTR_STR type, no resolving needed (list:%d  %p)",
                        var->name_size, var->name, var->is_list_item,
                        var->list);
            return knd_OK;
    default:
        break;
    }

    if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2) {
            const char *attr_type_name = knd_attr_names[attr->type];
            size_t attr_type_name_size = strlen(attr_type_name);
            knd_log(".. class \"%.*s\" to resolve attr item list \"%.*s\" (%p)"
                    " of CLASS:%.*s   attr type:%.*s",
                    self->entry->name_size, self->entry->name, var->name_size, var->name, var,
                    attr->ref_classname_size, attr->ref_classname,
                    attr_type_name_size, attr_type_name);
    }

    /* resolve template class ref */
    if (!attr->ref_class_entry) {
        err = knd_resolve_class_ref(self, attr->ref_classname, attr->ref_classname_size, NULL, &c, task);
        if (err) {
            knd_log("-- ref not resolved: :%.*s",
                    attr->ref_classname, attr->ref_classname_size);
            return err;
        }
        attr->ref_class_entry = c->entry;
    }

    /* base template class */
    entry = attr->ref_class_entry;
    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class \"%.*s\"", entry->name_size, entry->name);
    if (!c->is_resolved) {
        err = knd_class_resolve(c, task);
        KND_TASK_ERR("failed to resolve class \"%.*s\"", c->name_size, c->name);
    }

    /* does local repo have a clone of this class? */
    if (self->entry->repo != c->entry->repo) {
        err = knd_get_class(self->entry->repo, entry->name, entry->name_size, &local_class, task);
        if (!err)
            c = local_class;
    }

    if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2)
        c->str(c, 1);

    FOREACH (item, var->list) {
        item->attr = attr;

        switch (attr->type) {
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

static int resolve_attr_ref(struct kndClass *self, struct kndAttrVar *parent_item, struct kndTask *task)
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

    if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2)
	knd_log(".. resolving attr ref %.*s..", parent_item->name_size, parent_item->name);

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
                      "failed to resolve the \"%.*s\" attr ref",
                      classname_size, classname, parent_item->name_size, parent_item->name);
    }

    /* get attr name */
    attrname = parent_item->val;
    attrname_size = parent_item->val_size;
    if (!attrname_size) {
        err = knd_FAIL;
	KND_TASK_ERR("no attr name specified in attr ref \"%.*s\"",
                     parent_item->name_size, parent_item->name);
    }
    err = knd_class_get_attr(entry->class, attrname, attrname_size, &attr_ref);
    if (err) {
	KND_TASK_ERR("no attr \"%.*s\" in class \"%.*s\"",
                     attrname_size, attrname, entry->class->name_size, entry->class->name);
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

int knd_resolve_attr_vars(struct kndClass *self, struct kndClassVar *cvar, struct kndTask *task)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndAttrVar *var;
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    struct kndClass *c;
    struct kndProc *proc;
    struct kndRepo *repo = self->entry->repo;
    int err;

    if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2) {
        knd_log(".. resolving attr vars of class \"%.*s\" (base:%.*s) (repo:%.*s) ..",
                self->entry->name_size, self->entry->name, cvar->entry->name_size, cvar->entry->name,
                repo->name_size, repo->name);
    }

    FOREACH (var, cvar->attrs) {
        if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2)
            knd_log(".. resolving attr var: %.*s", var->name_size, var->name);

        if (!memcmp("_proc", var->name, var->name_size)) {
            if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_3) 
                knd_log(".. resolve _proc ref %.*s", var->val_size, var->val);
            continue;
        }

        err = knd_class_get_attr(self, var->name, var->name_size, &attr_ref);
        KND_TASK_ERR("no attr \"%.*s\" in class \"%.*s\"", var->name_size, var->name, self->name_size, self->name);
        attr = attr_ref->attr;
        attr_ref->attr_var = var;

        if (DEBUG_ATTR_VAR_RESOLVE_LEVEL_2) {
            knd_log("\n.. resolving attr vars of class \"%.*s\" (repo:%.*s) ..",
                    self->entry->name_size, self->entry->name,
                    repo->name_size, repo->name);
            knd_log("++ got attr: %.*s [id:%.*s] indexed:%d",
                    attr->name_size, attr->name,
                    attr->id_size, attr->id, attr->is_indexed);
            knd_attr_var_str(var, 1);
        }

        if (attr->is_a_set) {
            var->attr = attr;

            err = resolve_attr_var_list(self, var, task);
            KND_TASK_ERR("attr var list not resolved: %.*s", var->name_size, var->name);

            if (var->val_size)
                var->num_list_elems++;

            /*if (attr->is_indexed) {
                err = knd_index_attr_var_list(self, attr, var, task);
                if (err) return err;
                }*/

            /*task->attr = attr;
            switch (attr->type) {
            case KND_ATTR_INNER:
                for (item = var->list; item; item = item->next) {
                    err = knd_index_inner_var(self, item, task);             RET_ERR();
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
            var->attr = attr;
            err = resolve_inner_var(self, var, task);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            c = attr->ref_class_entry->class;
            if (!c->is_resolved) {
                err = knd_class_resolve(c, task);
                KND_TASK_ERR("failed to resolve class \"%.*s\"", c->name_size, c->name);
            }
            err = knd_resolve_class_ref(self, var->val, var->val_size, c, &var->class, task);
            if (err) return err;
            break;
        case KND_ATTR_TEXT:
            var->attr = attr;
            err = resolve_text(var, task);
            KND_TASK_ERR("failed to resolve text attr");
            break;
        case KND_ATTR_NUM:
            memcpy(buf, var->val, var->val_size);
            buf_size = var->val_size;
            buf[buf_size] = '\0';
            err = knd_parse_num(buf, &var->numval);
            break;
        case KND_ATTR_ATTR_REF:
            err = resolve_attr_ref(self, var, task);
            if (err) return err;
            break;
        case KND_ATTR_PROC_REF:
            proc = attr->proc;
            err = knd_resolve_proc_ref(self, var->val, var->val_size, proc, &var->proc, task);
            if (err) return err;
            break;
        default:
            /* atomic value, call a validation function? */
            break;
        }
        var->attr = attr;
    }
    return knd_OK;
}
