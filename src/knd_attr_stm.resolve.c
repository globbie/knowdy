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
#include "knd_attr_stm.h"
#include "knd_quant.h"
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

#define DEBUG_ATTR_STM_RESOLVE_LEVEL_1 0
#define DEBUG_ATTR_STM_RESOLVE_LEVEL_2 0
#define DEBUG_ATTR_STM_RESOLVE_LEVEL_3 0
#define DEBUG_ATTR_STM_RESOLVE_LEVEL_4 0
#define DEBUG_ATTR_STM_RESOLVE_LEVEL_5 0
#define DEBUG_ATTR_STM_RESOLVE_LEVEL_TMP 1

static int resolve_attr_stm_list(struct kndRepo *repo, struct kndAttrStm *parent_item,
                                 struct kndTask *task);
static int resolve_ref(struct kndRepo *repo, struct kndAttrStm *var,
                       struct kndTask *task);

static int resolve_implied_attr_stm(struct kndRepo *repo, struct kndAttr *attr,
                                    struct kndAttrStm *stm, struct kndTask *task)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    const char *classname;
    size_t classname_size = 0;
    int err;

    if (DEBUG_ATTR_STM_RESOLVE_LEVEL_2) {
        const char *attr_type_name = knd_attr_names[attr->type];
        size_t attr_type_name_size = strlen(attr_type_name);
        knd_log(".. resolving implied {class %.*s {%.*s %.*s {impl %d} {req %d} {val %.*s}}}",
                attr->owner->name_size, attr->owner->name,
                attr_type_name_size, attr_type_name, attr->name_size, attr->name,
                attr->is_implied, attr->is_required, stm->val_size, stm->val);
        knd_log(":: {is-list-item %d} {is-a-set %d}",
                stm->is_list_item, attr->is_a_set);
    }

    stm->implied_attr = attr;

    classname = stm->val;
    classname_size = stm->val_size;

    if (stm->is_list_item) {
        if (!stm->val_size) {
            classname = stm->name;
            classname_size = stm->name_size;
        }
    }

    switch (attr->type) {
    case KND_ATTR_UINT:
        if (stm->val_size) {
            memcpy(buf, stm->val, stm->val_size);
            buf_size = stm->val_size;
            buf[buf_size] = '\0';
            //err = knd_parse_int(buf, &stm->numval);
            //KND_TASK_ERR("failed to parse num %.*s", buf_size, buf);
            // TODO: float parsing
        }
        break;
    case KND_ATTR_REF:
        if (classname_size) {
            stm->val = classname;
            stm->val_size = classname_size;
        } else {
            if (attr->is_required) {
                KND_TASK_LOG("{class %.*s {implied-attr %.*s}} cannot be empty",
                             attr->owner->name_size, attr->owner->name,
                             attr->name_size, attr->name);
                return knd_FORMAT;
           }
           // empty val, no resolving needed
           break;
        }

        err = resolve_ref(repo, stm, task);
        if (err) return err;
        break;
    case KND_ATTR_REL:
        if (classname_size) {
            stm->val = classname;
            stm->val_size = classname_size;
        } else {
            if (attr->is_required) {
                KND_TASK_LOG("{class %.*s {implied-attr %.*s}} cannot be empty",
                             attr->owner->name_size, attr->owner->name,
                             attr->name_size, attr->name);
                return knd_FORMAT;
           }
           // empty val, no resolving needed
           break;
        }
        err = knd_rel_pred_resolve(stm, task);
        if (err) return err;
        break;
    case KND_ATTR_STR:
        // TODO: check enum values
        break;
    default:
        break;
    }
    return knd_OK;
}

static int resolve_inner_attr(struct kndRepo *repo, struct kndAttrStm *stm, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndClass *c;
    struct kndAttrStm *item;
    struct kndAttr *attr = stm->attr;
    struct kndAttrRef *attr_ref;
    struct kndQuantUInt *uint;
    struct kndProc *proc;
    struct kndSharedDict *class_name_idx = task->idxs->class_name_idx;
    int err;

    if (stm->is_list_item) {
        attr = stm->parent->attr;
    }

    if (DEBUG_ATTR_STM_RESOLVE_LEVEL_2) {
        knd_log(".. resolve {inner %.*s {is-list-item %d}} "
                " {val %.*s} {template %.*s {subclass %.*s}}",
                stm->name_size, stm->name,
                stm->is_list_item, stm->val_size, stm->val,
                attr->classname_size, attr->classname,
                stm->class_name_size, stm->class_name);
    }

    assert (attr->classname_size != 0 && attr->classname != NULL);

    /* default class template */
    entry = attr->class_entry;

    /* explicit subclass is set */
    if (stm->class_name_size) {
        entry = knd_shared_dict_get(class_name_idx, stm->class_name, stm->class_name_size);
        if (!entry) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("no such {class %.*s} .."
                         "failed to resolve {attr %.*s}",
                         stm->class_name_size, stm->class_name,
                         stm->name_size, stm->name);
        }
    }
    
    if (entry) {
        err = knd_class_acquire(entry, &c, task);
        KND_TASK_ERR("failed to acquire {class %.*s}", entry->name_size, entry->name);
    } else {
        err = knd_resolve_class_ref(repo, attr->classname, attr->classname_size,
                                    NULL, &c, task);
        KND_TASK_ERR("failed to resolve class ref %.*s",
                     attr->classname_size, attr->classname);
        attr->class_entry = c->entry;
    }

    if (c->phase < KND_CLASS_RESOLVED) {
        err = knd_class_resolve(c, task);
        KND_TASK_ERR("failed to resolve class %.*s", c->name_size, c->name);
    }

    if (stm->list) {
        err = resolve_attr_stm_list(repo, stm, task);
        if (err) return err;
        return knd_OK;
    }

    if (c->implied_attr) {
        err = resolve_implied_attr_stm(repo, c->implied_attr, stm, task);
        KND_TASK_ERR("failed to resolve implied attr stm");
    }

    FOREACH (item, stm->children) {
        err = knd_class_get_attr(c, item->name, item->name_size, &attr_ref);
        KND_TASK_ERR("no {attr %.*s} in {class %.*s}",
                     item->name_size, item->name, c->name_size, c->name);

        attr = attr_ref->attr;
        item->attr = attr;

        if (attr->is_a_set) {
            err = resolve_attr_stm_list(repo, item, task);
            if (err) return err;
            continue;
        }

        switch (attr->type) {
        case KND_ATTR_UINT:
            err = knd_quant_parse_uint(item->val, item->val_size, &uint, task);
            KND_TASK_ERR("failed to parse uint value");
            item->subtype = uint;
            break;
        case KND_ATTR_UREAL:
            //err = parse_ureal_value(item, task);
            //KND_TASK_ERR("failed to parse ureal value");
            break;
        case KND_ATTR_INNER:
            err = resolve_inner_attr(repo, item, task);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = resolve_ref(repo, item, task);
            if (err) return err;
            break;
        case KND_ATTR_REL:
            err = knd_rel_pred_resolve(item, task);
            if (err) return err;
            break;
        case KND_ATTR_TEXT:
            item->attr = attr;
            err = knd_text_resolve(item, task);
            KND_TASK_ERR("failed to resolve text attr");
            break;
        case KND_ATTR_PROC_REF:
            proc = attr->proc;
            err = knd_resolve_proc_ref(item->val, item->val_size, proc, &item->proc_entry, task);
            if (err) return err;
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

static int resolve_attr_stm_list(struct kndRepo *repo, struct kndAttrStm *stm,
                                 struct kndTask *task)
{
    struct kndAttr *attr = stm->attr;
    struct kndAttrStm *item;
    struct kndClass *c;
    int err;

    assert(stm->list != NULL);

    switch (attr->type) {
        case KND_ATTR_STR:
            if (DEBUG_ATTR_STM_RESOLVE_LEVEL_2)
                knd_log("NB: \"%.*s\" has ATTR_STR type, no resolving needed",
                        stm->name_size, stm->name);
            return knd_OK;
    default:
        break;
    }

    if (DEBUG_ATTR_STM_RESOLVE_LEVEL_2) {
            const char *attr_type_name = knd_attr_names[attr->type];
            size_t attr_type_name_size = strlen(attr_type_name);
            knd_log(".. resolving {attr %.*s {type %.*s {set}} {cls %.*s}}",
                    stm->name_size, stm->name, attr_type_name_size, attr_type_name,
                    attr->classname_size, attr->classname);
    }

    /* resolve template class ref */
    if (!attr->class_entry) {
        assert (attr->classname_size != 0 && attr->classname != NULL);

        err = knd_resolve_class_ref(repo, attr->classname, attr->classname_size,
                                    NULL, &c, task);
        if (err) {
            knd_log("-- ref not resolved: :%.*s", attr->classname, attr->classname_size);
            return err;
        }
        attr->class_entry = c->entry;
    }

    /* base template class */
    /*entry = attr->class_entry;
    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire {class %.*s}", entry->name_size, entry->name);

    if (c->phase < KND_CLASS_RESOLVED) {
        err = knd_class_resolve(c, task);
        KND_TASK_ERR("failed to resolve {class %.*s}", c->name_size, c->name);
    }
    */

    /* TODO: does local repo have a clone of this class? */
    /*if (self->entry->repo != c->entry->repo) {
        err = knd_get_class(self->entry->repo, entry->name, entry->name_size,
                            &local_class, task);
        if (!err)
            c = local_class;
            }*/

    FOREACH (item, stm->list) {
        item->attr = attr;
        if (!item->val_size) {
            item->val = item->name;
            item->val_size = item->name_size;
        }

        switch (attr->type) {
        case KND_ATTR_INNER:
            err = resolve_inner_attr(repo, item, task);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = resolve_ref(repo, item, task);
            if (err) return err;
            break;
        case KND_ATTR_REL:
            err = knd_rel_pred_resolve(item, task);
            if (err) return err;
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

static int resolve_attr_ref(struct kndAttrStm *parent_item, struct kndTask *task)
{
    struct kndSharedDict *class_name_idx = task->idxs->class_name_idx;
    const char *classname = NULL;
    size_t classname_size = 0;
    const char *attrname = NULL;
    size_t attrname_size = 0;
    const char *val_classname = NULL;
    size_t val_classname_size = 0;
    struct kndClassEntry *entry;
    struct kndClass *c;
    struct kndAttrStm *attr_stm = NULL;
    struct kndAttrRef *attr_ref;
    int err;

    if (DEBUG_ATTR_STM_RESOLVE_LEVEL_2)
	knd_log(".. resolving attr ref %.*s..", parent_item->name_size, parent_item->name);

    FOREACH (attr_stm, parent_item->children) {
	if (!memcmp(attr_stm->name, "cls", strlen("cls"))) {
	    classname = attr_stm->val;
	    classname_size = attr_stm->val_size;
	}
	if (!memcmp(attr_stm->name, "val", strlen("val"))) {
	    val_classname = attr_stm->val;
	    val_classname_size = attr_stm->val_size;
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
         KND_TASK_ERR("no such {class %.*s} .."
                      "failed to resolve {attr %.*s}",
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

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);

    err = knd_class_get_attr(c, attrname, attrname_size, &attr_ref);
    if (err) {
	KND_TASK_ERR("no attr \"%.*s\" in {class %.*s}",
                     attrname_size, attrname, entry->name_size, entry->name);
    }
    parent_item->class_entry = entry;
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

static int resolve_ref(struct kndRepo *repo, struct kndAttrStm *stm, struct kndTask *task)
{
    struct kndClass *c, *ref_c;
    struct kndClassEntry *entry;
    int err;

    assert (stm->val != NULL);
    assert (stm->val_size != 0);

    entry = stm->attr->class_entry;

    if (stm->implied_attr) {
        entry = stm->implied_attr->class_entry;        
    }

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire {class %.*s}", entry->name_size, entry->name);

    if (c->phase < KND_CLASS_RESOLVED) {
        err = knd_class_resolve(c, task);
        KND_TASK_ERR("failed to resolve {class %.*s}", c->name_size, c->name);
    }

    err = knd_resolve_class_ref(repo, stm->val, stm->val_size, c, &ref_c, task);
    if (err) return err;
    stm->class_entry = ref_c->entry;

    return knd_OK;
}

int knd_resolve_attr_stms(struct kndClass *self, struct kndClassBasePred *bp,
                          struct kndTask *task)
{
    struct kndAttrStm *stm;
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    struct kndProc *proc;
    struct kndQuantUInt *uint;
    struct kndQuantUReal *ureal;
    int err;

    if (DEBUG_ATTR_STM_RESOLVE_LEVEL_2) {
        knd_log(".. resolving attr stms of {class %.*s} from {baseclass %.*s}",
                self->name_size, self->name, bp->entry->name_size, bp->entry->name);
    }

    FOREACH (stm, bp->attr_stms) {
        err = knd_class_get_attr(self, stm->name, stm->name_size, &attr_ref);
        KND_TASK_ERR("no {attr %.*s} in {class %.*s}",
                     stm->name_size, stm->name, self->name_size, self->name);
        attr = attr_ref->attr;

        attr_ref->attr_stm = stm;
        stm->attr = attr;

        if (DEBUG_ATTR_STM_RESOLVE_LEVEL_3) {
            knd_log(".. resolving {attr-stm %.*s} {attr-type %s}",
                    stm->name_size, stm->name, knd_attr_names[attr->type]);
        }

        if (attr->is_a_set) {
            err = resolve_attr_stm_list(self->entry->repo, stm, task);
            KND_TASK_ERR("attr stm list not resolved: %.*s", stm->name_size, stm->name);

            if (stm->val_size)
                stm->num_list_elems++;
            continue;
        }

        switch (attr->type) {
        case KND_ATTR_INNER:
            err = resolve_inner_attr(self->entry->repo, stm, task);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = resolve_ref(self->entry->repo, stm, task);
            if (err) return err;
            break;
        case KND_ATTR_REL:
            err = knd_rel_pred_resolve(stm, task);
            if (err) return err;
            break;
        case KND_ATTR_TEXT:
            err = knd_text_resolve(stm, task);
            KND_TASK_ERR("failed to resolve text attr");
            break;
        case KND_ATTR_UINT:
            err = knd_quant_parse_uint(stm->val, stm->val_size, &uint, task);
            KND_TASK_ERR("failed to parse uint value");
            stm->subtype = uint;
            break;
        case KND_ATTR_UREAL:
            err = knd_quant_parse_ureal(stm->val, stm->val_size, &ureal, task);
            KND_TASK_ERR("failed to parse ureal value");
            stm->subtype = ureal;
            break;
        case KND_ATTR_ATTR_REF:
            err = resolve_attr_ref(stm, task);
            if (err) return err;
            break;
        case KND_ATTR_PROC_REF:
            proc = attr->proc;
            err = knd_resolve_proc_ref(stm->val, stm->val_size, proc, &stm->proc_entry, task);
            if (err) return err;
            break;
        default:
            /* atomic value, call a validation function? */
            break;
        }
    }
    return knd_OK;
}
