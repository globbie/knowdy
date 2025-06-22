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

static int register_attr(struct kndClass *self, struct kndAttr *attr, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndSharedDict *attr_name_idx = task->idxs->attr_name_idx;
    struct kndSharedSet *attr_idx = task->idxs->attr_idx;
    struct kndAttrRef *attr_ref, *attr_refs;
    const char *name = attr->name;
    size_t name_size = attr->name_size;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_3) {
        knd_log(".. register {class %.*s {attr %.*s}}",
                self->name_size, self->name, name_size, name);
    }
    err = knd_attr_ref_new(&attr_ref, mempool);
    KND_TASK_ERR("failed to alloc kndAttrRef")
    attr_ref->attr = attr;
    attr_ref->cls_entry = self->entry;

    /* generate unique attr id */
    attr->numid = atomic_fetch_add_explicit(&task->idxs->attr_id_count, 1, memory_order_relaxed);
    attr->numid++;
    knd_uid_create(attr->numid, attr->id, &attr->id_size);

    switch (task->type) {
    case KND_TASK_RESTORE:
        // fall through
    case KND_TASK_BULK_LOAD:
        attr_refs = knd_shared_dict_get(attr_name_idx, name, name_size);
        if (!attr_refs) {
            err = knd_shared_dict_set(attr_name_idx, attr->name, attr->name_size,
                                      (void*)attr_ref);
            KND_TASK_ERR("failed to globally register attr name \"%.*s\"", name_size, name);
        } else {
            if (attr_refs->tail) {
                attr_refs->tail->next = attr_ref;
                attr_refs->tail = attr_ref;
            } else {
                attr_refs->next = attr_ref;
            }
            attr_refs->tail = attr_ref;
        }

        err = knd_shared_set_add(attr_idx, attr->id, attr->id_size, (void*)attr_ref);
        KND_TASK_ERR("failed to globally register numid of attr \"%.*s\"", name_size, name);

        err = knd_set_add(self->attr_idx, attr->id, attr->id_size, (void*)attr_ref);
        KND_TASK_ERR("failed to locally register numid of attr \"%.*s\"", name_size, name);
        return knd_OK;
    default:
        break;
    }

    /* local task name idx */
    err = knd_dict_set(task->attr_name_idx, name, name_size, (void*)attr_ref);
    KND_TASK_ERR("failed to register attr name %.*s", name_size, name);

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        knd_log("++ commit import: new primary attr registered: \"%.*s\" (id:%.*s)",
                name_size, name, attr->id_size, attr->id);

    return knd_OK;
}

static int check_attr_name_conflict(struct kndClass *self, struct kndAttr *attr_candidate,
                                    struct kndTask *task)
{
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    void *obj;
    struct kndSet *attr_idx = self->attr_idx;
    struct kndSharedDict *attr_name_idx = task->idxs->attr_name_idx;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
        knd_log(".. checking attr name conflict: %.*s",
                attr_candidate->name_size, attr_candidate->name);
    }

    /* global attr name search */
    attr_ref = knd_shared_dict_get(attr_name_idx, attr_candidate->name, attr_candidate->name_size);
    if (!attr_ref) return knd_OK;

    while (attr_ref) {
        attr = attr_ref->attr;

        err = knd_set_get(attr_idx, attr->id, attr->id_size, &obj);
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

int knd_attr_resolve(struct kndAttr *attr, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndProcEntry *proc_entry;
    struct kndClassInnerAttr *cls_inner_attr;
    struct kndClassRefAttr *cls_ref_attr;
    struct kndSharedDict *class_name_idx = task->idxs->class_name_idx;
    int err;

    switch (attr->type) {
    case KND_ATTR_DATE:
        // fall through
    case KND_ATTR_STR:
        if (attr->format_cls_name_size) {
            entry = knd_shared_dict_get(class_name_idx,
                                        attr->format_cls_name, attr->format_cls_name_size);
            if (!entry) {
                err = knd_NO_MATCH;
                KND_TASK_ERR("class not found: \"%.*s\"",
                             attr->format_cls_name_size, attr->format_cls_name);
            }
            attr->format_cls_entry = entry;
        }
        break;
    case KND_ATTR_CLS_INNER:
        cls_inner_attr = attr->subtype;
        if (!attr->cls_name_size) {
            err = knd_FAIL;
            KND_TASK_ERR("no class specified for {attr %.*s}",
                         attr->name_size, attr->name);
        }
        entry = knd_shared_dict_get(class_name_idx, attr->cls_name, attr->cls_name_size);
        if (!entry) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("no such {class %.*s}",
                         attr->cls_name_size, attr->cls_name);
        }
        cls_inner_attr->template_cls = entry;
        break;
    case KND_ATTR_CLS_REF:
        cls_ref_attr = attr->subtype;
        if (!attr->cls_name_size) {
            err = knd_FAIL;
            KND_TASK_ERR("no template class specified for {attr %.*s}",
                         attr->name_size, attr->name);
        }
        entry = knd_shared_dict_get(class_name_idx,
                                    attr->cls_name, attr->cls_name_size);
        if (!entry) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("no such {cls %.*s}",
                         attr->cls_name_size, attr->cls_name);
        }
        cls_ref_attr->template_cls = entry;
        break;
    case KND_ATTR_PROC_REF:
        if (!attr->ref_proc_name_size) {
            knd_log("-- no proc name specified for attr \"%.*s\"", attr->name_size, attr->name);
            return knd_FAIL;
        }
        proc_entry = knd_shared_dict_get(task->idxs->proc_name_idx,
                                         attr->ref_proc_name, attr->ref_proc_name_size);
        if (!proc_entry) {
            knd_log("-- no such proc: \"%.*s\" .."
                    "couldn't resolve the \"%.*s\" attr of %.*s :(",
                    attr->ref_proc_name_size,
                    attr->ref_proc_name,
                    attr->name_size, attr->name,
                    attr->owner->name_size, attr->owner->name);
            return knd_FAIL;
        }
        if (DEBUG_ATTR_RESOLVE_LEVEL_2)
            knd_log("++ proc ref resolved: %.*s!",
                    proc_entry->name_size, proc_entry->name);
        break;
    default:
        break;
    }
    return knd_OK;
}

int knd_resolve_primary_attrs(struct kndClass *self, struct kndTask *task)
{
    struct kndAttr *attr;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
        knd_log(".. resolving primary attrs of {cls %.*s {total-attrs %zu}}",
                self->name_size, self->name, self->num_attrs);
    }

    FOREACH (attr, self->attrs) {
        err = check_attr_name_conflict(self, attr, task);
        KND_TASK_ERR("name conflict detected");

        err = knd_attr_resolve(attr, task);
        KND_TASK_ERR("failed to resolve attr");

        err = register_attr(self, attr, task);
        KND_TASK_ERR("failed to register new attr");
    }
    return knd_OK;
}
