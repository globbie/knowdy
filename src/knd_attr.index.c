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

#define DEBUG_ATTR_INDEX_LEVEL_1 0
#define DEBUG_ATTR_INDEX_LEVEL_2 0
#define DEBUG_ATTR_INDEX_LEVEL_3 0
#define DEBUG_ATTR_INDEX_LEVEL_4 0
#define DEBUG_ATTR_INDEX_LEVEL_5 0
#define DEBUG_ATTR_INDEX_LEVEL_TMP 1

static int attr_hub_fetch(struct kndClass *owner, struct kndAttr *attr, struct kndAttrHub **result,
                          struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndAttrHub *hub = NULL;
    int err;
    FOREACH (hub, owner->attr_hubs) {
        if (hub->attr == attr) {
            break;
        }
    }
    if (!hub) {
        err = knd_attr_hub_new(mempool, &hub);
        KND_TASK_ERR("failed to alloc attr hub");
        // hub->topic_template = topic;
        hub->attr = attr;
        // hub->owner = owner;
        hub->next = owner->attr_hubs;
        owner->attr_hubs = hub;
    }
    *result = hub;
    return knd_OK;
}

int knd_attr_hub_update(struct kndClass *owner, struct kndClassEntry *topic, struct kndClassInstEntry *topic_inst,
                        struct kndClassEntry *spec, struct kndClassInstEntry *spec_inst,
                        struct kndAttr *attr, struct kndAttrVar *unused_var(var), struct kndTask *task, bool unused_var(is_ancestor))
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndAttrHub *hub;
    struct kndSet *set;
    struct kndClassRef *ref;
    struct kndClassInstRef *inst_ref;
    int err;

    if (DEBUG_ATTR_INDEX_LEVEL_2) {
        knd_log(".. attr hub \"%.*s\" of class \"%.*s\" to add topic \"%.*s\" (inst:%p) => spec \"%.*s\" (inst:%p)",
                attr->name_size, attr->name, owner->name_size, owner->name,
                topic->name_size, topic->name, topic_inst, spec->name_size, spec->name, spec_inst);
    }

    err = attr_hub_fetch(owner, attr, &hub, task);
    KND_TASK_ERR("failed to fetch attr hub");

    set = hub->topics;
    if (!set) {
        err = knd_set_new(mempool, &set);
        KND_TASK_ERR("failed to alloc topic set for attr hub");
        hub->topics = set;
    }

    /* topic already registered? */
    err = knd_set_get(set, topic->id, topic->id_size, (void**)&ref);
    if (err) {
        err = knd_class_ref_new(mempool, &ref);
        KND_TASK_ERR("failed to alloc class ref");
        ref->entry = topic;

        err = knd_set_add(set, topic->id, topic->id_size, (void*)ref);
        KND_TASK_ERR("failed to register class ref");
    }

    if (topic_inst) {
        err = knd_class_inst_ref_new(mempool, &inst_ref);
        KND_TASK_ERR("failed to alloc a class inst ref");
        inst_ref->entry = topic_inst;

        inst_ref->next = ref->insts;
        ref->insts = inst_ref;
    }
    return knd_OK;
}

int knd_attr_index(struct kndClass *self, struct kndAttr *attr, struct kndTask *task)
{
    struct kndClass *base;
    int err;

    if (DEBUG_ATTR_INDEX_LEVEL_2) {
        knd_log(".. indexing attr \"%.*s\" [type:%d] of class \"%.*s\" (id:%.*s)"
                " refclass: \"%.*s\"",
                attr->name_size, attr->name, attr->type,
                self->entry->name_size, self->entry->name,
                self->entry->id_size, self->entry->id,
                attr->ref_classname_size, attr->ref_classname);
    }
    if (!attr->ref_classname_size) return knd_OK;

    /* template base class */
    err = knd_get_class(self->entry->repo, attr->ref_classname, attr->ref_classname_size, &base, task);
    KND_TASK_ERR("failed to get class %.*s", attr->ref_classname_size, attr->ref_classname);
    if (!base->is_resolved) {
        err = knd_class_resolve(base, task);
        KND_TASK_ERR("failed to resolve base class %.*s", base->name_size, base->name);
    }
    return knd_OK;
}


