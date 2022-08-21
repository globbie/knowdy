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
        hub->attr = attr;
        hub->next = owner->attr_hubs;
        owner->attr_hubs = hub;
    }

    *result = hub;
    return knd_OK;
}

#if 0
static int attr_hub_fetch_child(struct kndAttrHub *parent, struct kndAttr *attr,
                                struct kndAttrHub **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndAttrHub *hub = NULL;
    int err;

    FOREACH (hub, parent->children) {
        if (hub->attr == attr) {
            break;
        }
    }
    if (!hub) {
        err = knd_attr_hub_new(mempool, &hub);
        KND_TASK_ERR("failed to alloc attr hub");
        hub->attr = attr;
        hub->next = parent->children;
        parent->children = hub;
    }
    *result = hub;
    return knd_OK;
}
#endif

static int attr_hub_add_classref(struct kndAttrHub *hub, struct kndClassEntry *topic,
                                 struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;   
    struct kndSet *set;
    struct kndClassRef *ref;
    int err;

    if (DEBUG_ATTR_VAR_IDX_LEVEL_3) {
        knd_log(".. attr hub \"%.*s\" to add topic \"%.*s\" => spec \"%.*s\"",
                hub->attr->name_size, hub->attr->name, 
                topic->name_size, topic->name);
    }
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
    return knd_OK;
}

static int index_inner_attr_var(struct kndClassEntry *topic, struct kndAttr *attr,
                                struct kndAttrVar *var, struct kndTask *task)
{
    struct kndClass *spec;
    struct kndAttrVar *item;
    struct kndAttrHub *hub;
    int err;

    if (var->implied_attr) {
        switch (var->implied_attr->type) {
        case KND_ATTR_REF:
            // fall through
        case KND_ATTR_REL:
            assert(var->class_entry != NULL);
            err = knd_class_acquire(var->class_entry, &spec, task);
            KND_TASK_ERR("failed to acquire class %.*s",
                         var->class_entry->name_size, var->class_entry->name);

            if (DEBUG_ATTR_VAR_IDX_LEVEL_TMP)
                knd_log(">> idx inner implied rel {class %.*s {%.*s %.*s}}",
                        topic->name_size, topic->name,
                        var->name_size, var->name, spec->name_size, spec->name);

            err = attr_hub_fetch(spec, attr, &hub, task);
            KND_TASK_ERR("failed to fetch attr hub");

            err = attr_hub_add_classref(hub, topic, task);
            KND_TASK_ERR("attr hub failed to add a classref");
            break;
        default:
            break;
        }
    }

    /* index nested children */
    FOREACH (item, var->children) {
        if (!item->attr->is_indexed) continue;

        switch (item->attr->type) {
        case KND_ATTR_INNER:
            // TODO
            break;
        case KND_ATTR_REF:
            // fall through
        case KND_ATTR_REL:
            assert(item->class_entry != NULL);
            err = knd_class_acquire(item->class_entry, &spec, task);
            KND_TASK_ERR("failed to acquire class %.*s",
                         item->class_entry->name_size, item->class_entry->name);

            err = attr_hub_fetch(spec, attr, &hub, task);
            KND_TASK_ERR("failed to fetch attr hub");

            if (DEBUG_ATTR_VAR_IDX_LEVEL_3)
                knd_log("[TODO] .. index {inner %.*s} rel attr {%.*s %.*s} {implied-attr %p}",
                        var->name_size, var->name, item->name_size, item->name,
                        item->val_size, item->val, item->implied_attr);
            err = attr_hub_add_classref(hub, topic, task);
            KND_TASK_ERR("attr hub failed to add a classref");
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

int knd_index_attr_var(struct kndClassEntry *topic,
                       struct kndClassInstEntry *unused_var(topic_inst),
                       struct kndAttr *attr, struct kndAttrVar *var, struct kndTask *task)
{
    struct kndClass *spec;
    struct kndAttrHub *hub;
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

        err = attr_hub_fetch(spec, attr, &hub, task);
        KND_TASK_ERR("failed to fetch attr hub");

        err = attr_hub_add_classref(hub, topic, task);
        KND_TASK_ERR("attr hub failed to add a classref");
        break;
    case KND_ATTR_INNER:
        err = index_inner_attr_var(topic, attr, var, task);
        KND_TASK_ERR("failed to index inner attr var");
        break;
    default:
        break;
    }
    return knd_OK;
}

int knd_index_attr_var_list(struct kndClassEntry *topic,
                            struct kndClassInstEntry *unused_var(topic_inst),
                            struct kndAttr *attr, struct kndAttrVar *var, struct kndTask *task)
{
    struct kndClass *spec;
    struct kndAttrVar *item;
    struct kndAttrHub *hub;
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

            err = attr_hub_fetch(spec, attr, &hub, task);
            KND_TASK_ERR("failed to fetch attr hub");

            err = attr_hub_add_classref(hub, topic, task);
            KND_TASK_ERR("attr hub failed to add a classref");
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

