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

static int register_attr(struct kndClass *self, struct kndAttr *attr, struct kndTask *task)
{
    struct kndRepo *repo =       self->entry->repo;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *attr_idx    = repo->attr_idx;
    struct kndAttrRef *attr_ref, *next_attr_ref;
    const char *name = attr->name;
    size_t name_size = attr->name_size;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        knd_log(".. register attr: %.*s (host class: %.*s) task type:%d",
                name_size, name, self->name_size, self->name, task->type);

    err = knd_attr_ref_new(mempool, &attr_ref);
    KND_TASK_ERR("failed to alloc kndAttrRef")
    attr_ref->attr = attr;
    attr_ref->class_entry = self->entry;

    /* generate unique attr id */
    attr->numid = atomic_fetch_add_explicit(&repo->attr_id_count, 1, memory_order_relaxed);
    attr->numid++;
    knd_uid_create(attr->numid, attr->id, &attr->id_size);

    switch (task->type) {
    case KND_RESTORE_STATE:
        // fall through
    case KND_LOAD_STATE:
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

int knd_resolve_primary_attrs(struct kndClass *self, struct kndTask *task)
{
    struct kndAttr *attr;
    struct kndClassEntry *entry;
    struct kndProcEntry *proc_entry;
    struct kndRepo *repo = self->entry->repo;
    struct kndSharedDict *class_name_idx = repo->class_name_idx;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2)
        knd_log(".. resolving primary attrs of %.*s [total:%zu]",
                self->name_size, self->name, self->num_attrs);

    FOREACH (attr, self->attrs) {
        err = check_attr_name_conflict(self, attr, task);
        KND_TASK_ERR("name conflict detected");

        switch (attr->type) {
        case KND_ATTR_INNER:
            // fall through
        case KND_ATTR_REF:
            if (!attr->ref_classname_size) {
                err = knd_FAIL;
                KND_TASK_ERR("no class specified for attr \"%s\"", attr->name);
            }
            entry = knd_shared_dict_get(class_name_idx, attr->ref_classname, attr->ref_classname_size);
            if (!entry) {
                err = knd_NO_MATCH;
                KND_TASK_ERR("class not found: \"%.*s\"", attr->ref_classname_size, attr->ref_classname);
            }
            attr->ref_class_entry = entry;
            break;
        case KND_ATTR_PROC_REF:
            if (!attr->ref_procname_size) {
                knd_log("-- no proc name specified for attr \"%.*s\"", attr->name_size, attr->name);
                return knd_FAIL;
            }
            proc_entry = knd_shared_dict_get(repo->proc_name_idx, attr->ref_procname, attr->ref_procname_size);
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
        err = register_attr(self, attr, task);
        KND_TASK_ERR("failed to register new attr");
    }
    return knd_OK;
}
