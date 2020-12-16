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

#define DEBUG_ATTR_VAR_INDEX_LEVEL_1 0
#define DEBUG_ATTR_VAR_INDEX_LEVEL_2 0
#define DEBUG_ATTR_VAR_INDEX_LEVEL_3 0
#define DEBUG_ATTR_VAR_INDEX_LEVEL_4 0
#define DEBUG_ATTR_VAR_INDEX_LEVEL_5 0
#define DEBUG_ATTR_VAR_INDEX_LEVEL_TMP 1

static void str_hub_path(struct kndAttrVar *item)
{
    if (!item->parent) return;
    knd_log("   in \"%.*s\"", item->parent->name_size, item->parent->name);
    str_hub_path(item->parent);
}

#if 0
static int update_attr_facet(struct kndAttr *attr, struct kndClass *topic, struct kndClass *spec, struct kndTask *task)
{
    struct kndSet *set;
    struct kndAttrFacet *facet;
    struct kndMemPool *mempool = task->mempool;
    void *entry;
    int err;

    /*knd_log("\n\n.. update the \"%.*s\" attr facet: topic:%.*s spec:%.*s task:%p",
            attr->name_size, attr->name,
            topic->name_size, topic->name,
            spec->name_size, spec->name, task);
    */
    set = attr->facet_idx;
    if (!set) {
        err = knd_set_new(mempool, &set);                                         RET_ERR();
        attr->facet_idx = set;
    }

    err = set->get(set,
                   spec->entry->id, spec->entry->id_size,
                   (void**)&facet);
    if (err) {
        err = knd_attr_facet_new(mempool, &facet);                                RET_ERR();
        err = knd_set_new(mempool, &facet->topics);                               RET_ERR();
        err = set->add(set, spec->entry->id, spec->entry->id_size,
                       (void*)facet);                                             RET_ERR();
    }

    err = facet->topics->get(facet->topics, topic->entry->id, topic->entry->id_size,
                             &entry);
    if (err == knd_OK) return knd_OK;

    err = facet->topics->add(facet->topics,
                             topic->entry->id, topic->entry->id_size,
                             (void*)topic->entry);                                RET_ERR();
    
    return knd_OK;
}
#endif

static int index_inner_class_ref(struct kndClass *self, struct kndAttrVar *item, struct kndAttr *attr, struct kndTask *task)
{
    // TODO atomic
    struct kndClass *base = attr->ref_class_entry->class;
    struct kndClass *topic = NULL; // task->attr->parent_class;
    struct kndClass *spec = item->class;
    int err;

    if (DEBUG_ATTR_VAR_INDEX_LEVEL_2) {
        knd_log("\n.. index path from \"%.*s\" (template:\"%.*s\")",
                spec->name_size, spec->name,
                base->name_size, base->name);
        knd_log("   as \"%.*s\"", attr->name_size, attr->name);
        str_hub_path(item);
        knd_log("   of \"%.*s\" (desc of: %.*s)",
                self->name_size, self->name,
                topic->name_size, topic->name);
    }

    err = knd_attr_hub_update(spec, self->entry, NULL, spec->entry, NULL, attr, item, task, false);
    RET_ERR();

    /* update the ancestors */
    /*FOREACH (ref, spec->ancestors) {
        curr_class = ref->class;
        err = knd_is_base(base, curr_class);
        if (err) continue;
        err = knd_attr_hub_update(self, spec, curr_class, item, attr, task, true);
        RET_ERR();
        }*/
    return knd_OK;
}

static int index_attr_var(struct kndClass *self, struct kndAttr *attr, struct kndAttrVar *item, struct kndTask *task)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    int err;

    switch (attr->type) {
    case KND_ATTR_NUM:
        if (!attr->is_indexed) break;

        if (DEBUG_ATTR_VAR_INDEX_LEVEL_2)
            knd_log(".. indexing num attr: %.*s val:%.*s",
                    item->name_size, item->name,
                    item->val_size, item->val);

        if (item->val_size) {
            memcpy(buf, item->val, item->val_size);
            buf_size = item->val_size;
            buf[buf_size] = '\0';
            
            err = knd_parse_num(buf, &item->numval);
            // TODO: float parsing
        }
        break;
    case KND_ATTR_INNER:
        if (DEBUG_ATTR_VAR_INDEX_LEVEL_2)
            knd_log("== nested inner item found: %.*s", item->name_size, item->name);
        err = knd_index_inner_attr_var(self, item, task);
        if (err) return err;
        break;
    case KND_ATTR_REF:
        if (!attr->is_indexed) break;
        err = index_inner_class_ref(self, item, attr, task);                      RET_ERR();
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

    if (DEBUG_ATTR_VAR_INDEX_LEVEL_2)
        knd_log(".. attr var list indexing (class:%.*s) attr \"%.*s\" [type:%d]",
                topic->name_size, topic->name, attr->name_size, attr->name, attr->type);
    
    FOREACH (item, var->list) {
        if (DEBUG_ATTR_VAR_INDEX_LEVEL_3)
            knd_log("* index list item: \"%.*s\"", item->name_size, item->name);

        switch (attr->type) {
        case KND_ATTR_REL:
            // fall through
        case KND_ATTR_REF:
            assert(item->class_entry != NULL);
            err = knd_class_acquire(item->class_entry, &spec, task);
            KND_TASK_ERR("failed to acquire class %.*s", item->class_entry->name_size, item->class_entry->name);

            if (item->class_inst_entry)
                spec_inst = item->class_inst_entry;

            /* update immediate hub */
            err = knd_attr_hub_update(spec, topic, topic_inst, item->class_entry, spec_inst, attr, item, task, false);
            KND_TASK_ERR("failed to update attr hub");
            
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

int knd_index_inner_attr_var(struct kndClass *self, struct kndAttrVar *var, struct kndTask *task)
{
    struct kndAttrVar *item;
    int err;

    if (DEBUG_ATTR_VAR_INDEX_LEVEL_2)
        knd_log(".. index inner attr var \"%.*s\" (val:%.*s) "
                " in class \"%.*s\" is list item:%d..",
                var->name_size, var->name, var->val_size, var->val,
                self->name_size, self->name, var->is_list_item);

    if (var->implied_attr) {
        err = index_attr_var(self, var->implied_attr, var, task);
        if (err) return err;
    }

    /* index nested children */
    FOREACH (item, var->children) {
        err = index_attr_var(self, item->attr, item, task);                      RET_ERR();
    }
    return knd_OK;
}
