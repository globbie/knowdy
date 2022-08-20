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

#define DEBUG_ATTR_VAR_IDX_LEVEL_1 0
#define DEBUG_ATTR_VAR_IDX_LEVEL_2 0
#define DEBUG_ATTR_VAR_IDX_LEVEL_3 0
#define DEBUG_ATTR_VAR_IDX_LEVEL_4 0
#define DEBUG_ATTR_VAR_IDX_LEVEL_5 0
#define DEBUG_ATTR_VAR_IDX_LEVEL_TMP 1

static int attr_hub_fetch(struct kndClass *owner, struct kndAttr *attr,
                          struct kndAttrHub **result, struct kndTask *task)
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

static int attr_hub_update(struct kndClass *owner, struct kndClassEntry *topic,
                           struct kndClassInstEntry *topic_inst,
                           struct kndClassEntry *spec, struct kndClassInstEntry *spec_inst,
                           struct kndAttr *attr, struct kndAttrVar *unused_var(var),
                           struct kndTask *task, bool unused_var(is_ancestor))
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndAttrHub *hub;
    struct kndSet *set;
    struct kndClassRef *ref;
    struct kndClassInstRef *inst_ref;
    int err;

    if (DEBUG_ATTR_VAR_IDX_LEVEL_TMP) {
        knd_log(".. attr hub \"%.*s\" of class \"%.*s\" "
                " to add topic \"%.*s\" (inst:%p) => spec \"%.*s\" (inst:%p)",
                attr->name_size, attr->name, owner->name_size, owner->name,
                topic->name_size, topic->name, topic_inst,
                spec->name_size, spec->name, spec_inst);
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

static int index_inner_attr_var(struct kndClassEntry *topic, struct kndClassInstEntry *topic_inst,
                                struct kndAttr *attr, struct kndAttrVar *var, struct kndTask *task)
{
    struct kndClass *spec;
    struct kndClassInstEntry *spec_inst = NULL;
    struct kndAttrVar *item;
    struct kndAttrHub *hub;
    int err;

    if (DEBUG_ATTR_VAR_IDX_LEVEL_TMP)
        knd_log(".. index {inner %.*s} from {class %.*s} is list item:%d.. {implied-attr %p}",
                var->name_size, var->name,
                topic->name_size, topic->name, var->is_list_item, var->implied_attr);

    err = attr_hub_fetch(topic->class, attr, &hub, task);
    KND_TASK_ERR("failed to fetch attr hub");

    if (var->implied_attr) {
        assert(var->class_entry != NULL);
        err = knd_class_acquire(var->class_entry, &spec, task);
        KND_TASK_ERR("failed to acquire class %.*s",
                     var->class_entry->name_size, var->class_entry->name);

        if (var->class_inst_entry)
            spec_inst = var->class_inst_entry;

        /* update immediate hub */
        err = attr_hub_update(spec, topic, topic_inst, var->class_entry,
                              spec_inst, var->implied_attr, var, task, false);
        KND_TASK_ERR("failed to update attr hub");
    }

    /* index nested children */
    FOREACH (item, var->children) {
        if (!item->attr->is_indexed) continue;

        if (DEBUG_ATTR_VAR_IDX_LEVEL_TMP)
            knd_log(".. index {inner %.*s} {%.*s %.*s} {implied-attr %p}",
                    var->name_size, var->name,
                    item->name_size, item->name,
                    item->val_size, item->val, item->implied_attr);
        
    }
    return knd_OK;
}

int knd_index_attr_var(struct kndClassEntry *topic, struct kndClassInstEntry *topic_inst,
                       struct kndAttr *attr, struct kndAttrVar *var, struct kndTask *task)
{
    struct kndClass *spec;
    struct kndClassInstEntry *spec_inst = NULL;
    int err;

    if (DEBUG_ATTR_VAR_IDX_LEVEL_2)
        knd_log(".. {class %.*s} to index {attr-var %.*s} {attr-type %d}",
                topic->name_size, topic->name, attr->name_size, attr->name, attr->type);

    switch (attr->type) {
    case KND_ATTR_REF:
        // fall through
    case KND_ATTR_REL:
        assert(var->class_entry != NULL);
        err = knd_class_acquire(var->class_entry, &spec, task);
        KND_TASK_ERR("failed to acquire class %.*s",
                     var->class_entry->name_size, var->class_entry->name);

        if (var->class_inst_entry)
            spec_inst = var->class_inst_entry;

        /* update immediate hub */
        err = attr_hub_update(spec, topic, topic_inst, var->class_entry,
                              spec_inst, attr, var, task, false);
        KND_TASK_ERR("failed to update attr hub");
        break;
    case KND_ATTR_INNER:
        err = index_inner_attr_var(topic, topic_inst, attr, var, task);
        KND_TASK_ERR("failed to index inner attr var");
        break;
    default:
        break;
    }
    return knd_OK;
}

int knd_index_attr_var_list(struct kndClassEntry *topic, struct kndClassInstEntry *topic_inst,
                            struct kndAttr *attr, struct kndAttrVar *var, struct kndTask *task)
{
    struct kndClass *spec;
    struct kndClassInstEntry *spec_inst = NULL;
    struct kndAttrVar *item;
    int err;

    if (DEBUG_ATTR_VAR_IDX_LEVEL_2)
        knd_log(".. attr var list indexing (class:%.*s) attr \"%.*s\" [type:%d]",
                topic->name_size, topic->name, attr->name_size, attr->name, attr->type);
    
    FOREACH (item, var->list) {
        if (DEBUG_ATTR_VAR_IDX_LEVEL_3)
            knd_log("* index list item: \"%.*s\"", item->name_size, item->name);

        switch (attr->type) {
        case KND_ATTR_REL:
            // fall through
        case KND_ATTR_REF:
            assert(item->class_entry != NULL);
            err = knd_class_acquire(item->class_entry, &spec, task);
            KND_TASK_ERR("failed to acquire class %.*s",
                         item->class_entry->name_size, item->class_entry->name);

            if (item->class_inst_entry)
                spec_inst = item->class_inst_entry;

            /* update immediate hub */
            err = attr_hub_update(spec, topic, topic_inst, item->class_entry,
                                  spec_inst, attr, item, task, false);
            KND_TASK_ERR("failed to update attr hub");
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

