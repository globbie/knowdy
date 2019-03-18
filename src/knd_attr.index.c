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

static int update_attr_hub(struct kndClass   *topic,
                           struct kndClass   *spec,
                           struct kndAttrHub **hubs,
                           struct kndAttrVar *item,
                           struct kndAttr    *attr,
                           struct kndTask    *task,
                           bool is_ancestor);

static int update_attr_facet(struct kndAttr *attr,
                             struct kndClass *topic,
                             struct kndClass *spec,
                             struct kndTask *task)
{
    struct kndSet *set;
    struct kndAttrFacet *facet;
    struct kndMemPool *mempool = task->mempool;
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
    
    err = facet->topics->add(facet->topics,
                             topic->entry->id, topic->entry->id_size,
                             (void*)topic->entry);                                RET_ERR();
    
    return knd_OK;
}

int knd_index_attr(struct kndClass *self,
                   struct kndAttr *attr,
                   struct kndAttrVar *item,
                   struct kndTask *task)
{
    struct kndClass *base;
    struct kndClassRef *ref;
    struct kndClass *c, *curr_class;
    int err;

    if (DEBUG_ATTR_INDEX_LEVEL_2) {
        knd_log("\n.. indexing CURR CLASS: \"%.*s\" .. index attr: \"%.*s\" [type:%d]"
                " refclass: \"%.*s\" (name:%.*s val:%.*s)",
                self->entry->name_size, self->entry->name,
                attr->name_size, attr->name, attr->type,
                attr->ref_classname_size, attr->ref_classname,
                item->name_size, item->name, item->val_size, item->val);
    }
    if (!attr->ref_classname_size) return knd_OK;

    /* template base class */
    err = knd_get_class(self->entry->repo,
                        attr->ref_classname,
                        attr->ref_classname_size,
                        &base, task);                                             RET_ERR();
    if (!base->is_resolved) {
        err = knd_class_resolve(base, task);                                      RET_ERR();
    }
    /* spec class */
    err = knd_get_class(self->entry->repo,
                        item->val, item->val_size, &c, task);                     RET_ERR();
    item->class = c;
    if (!c->is_resolved) {
        err = knd_class_resolve(c, task);                                         RET_ERR();
    }
    err = knd_is_base(base, c);                                                   RET_ERR();

    err = update_attr_hub(self, c, &c->entry->attr_hubs,
                          item, attr, task, false);                               RET_ERR();

    /* update the ancestors */
    for (ref = c->entry->ancestors; ref; ref = ref->next) {
        curr_class = ref->class;
        err = knd_is_base(base, curr_class);
        if (err) continue;
        err = update_attr_hub(self, c, &curr_class->entry->attr_hubs,
                              item, attr, task, true);                            RET_ERR();
    }
    return knd_OK;
}

extern int knd_index_attr_var_list(struct kndClass *self,
                                   struct kndAttr *attr,
                                   struct kndAttrVar *parent_item,
                                   struct kndTask *task)
{
    struct kndClass *base;
    struct kndClass *c, *curr_class;
    struct kndClassRef *ref;
    struct kndAttrVar *item = parent_item;
    const char *name;
    size_t name_size;
    int err;

    if (DEBUG_ATTR_INDEX_LEVEL_TMP) {
        knd_log("\n.. attr item list indexing.. (class:%.*s) "
                ".. index attr: \"%.*s\" [type:%d]"
                " refclass: \"%.*s\" %p (name:%.*s val:%.*s)",
                self->entry->name_size, self->entry->name,
                attr->name_size, attr->name, attr->type,
                attr->ref_classname_size, attr->ref_classname,
                attr->ref_class,
                item->name_size, item->name, item->val_size, item->val);
    }

    switch (attr->type) {
    case KND_ATTR_INNER:
        knd_log("..indexing inner class..\n");
        break;
    default:
        break;
    }

    if (!attr->ref_classname) return knd_OK;

    /* template base class */
    base = attr->ref_class;
    if (!base) {
        err = knd_get_class(self->entry->repo,
                            attr->ref_classname,
                            attr->ref_classname_size,
                            &base, task);                                             RET_ERR();
        if (!base->is_resolved) {
            err = knd_class_resolve(base, task);                                      RET_ERR();
            attr->ref_class = base;
        }
    }

    for (item = parent_item->list; item; item = item->next) {
        if (DEBUG_ATTR_INDEX_LEVEL_2)
            knd_log("== index list item: \"%.*s\" val:%.*s",
                    item->name_size, item->name,
                    item->val_size, item->val);
        name = item->name;
        name_size = item->name_size;
        if (item->val_size) {
            name = item->val;
            name_size = item->val_size;
        }

        /* spec class */
        err = knd_get_class(self->entry->repo,
                            name, name_size, &c, task);                           RET_ERR();
        item->class = c;
        if (!c->is_resolved) {
            err = knd_class_resolve(c, task);                                     RET_ERR();
        }
        err = knd_is_base(base, c);
        if (err) {
            knd_log("-- no inheritance from \"%.*s\" to \"%.*s\"",
                    base->name_size, base->name, c->name_size, c->name);
            return err;
        }

        /*** index direct rels ***/
        err = update_attr_facet(item->attr, self, c, task);                       RET_ERR();
        
        /*** index inverse rels ***/

        /* update immediate hub */
        err = update_attr_hub(self, c, &c->entry->attr_hubs,
                              item, item->attr, task, false);                     RET_ERR();
        /* update the ancestors */
        for (ref = c->entry->ancestors; ref; ref = ref->next) {
            curr_class = ref->class;
            if (base == curr_class) goto update_hub;

            err = knd_is_base(base, curr_class);
            if (err) continue;

        update_hub:
            if (DEBUG_ATTR_INDEX_LEVEL_TMP)
                knd_log(".. updating the attr hub of \"%.*s\"",
                        curr_class->name_size, curr_class->name);
            err = update_attr_hub(self, c, &curr_class->entry->attr_hubs,
                                  item, item->attr, task, true);                  RET_ERR();
        }

    }
    return knd_OK;
}

static void str_hub_path(struct kndAttrVar *item)
{
    if (!item->parent) return;
    knd_log("   in \"%.*s\"", item->parent->name_size, item->parent->name);
    str_hub_path(item->parent);
}

static int update_attr_hub(struct kndClass   *topic,
                           struct kndClass   *spec,
                           struct kndAttrHub **hubs,
                           struct kndAttrVar *item,
                           struct kndAttr    *attr,
                           struct kndTask    *task,
                           bool is_ancestor)
{
    struct kndAttrHub *hub = NULL;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *set;
    int err;

    for (hub = *hubs; hub; hub = hub->next) {
        if (hub->attr == attr) {
            if (DEBUG_ATTR_INDEX_LEVEL_2)
                knd_log("++ got attr hub: %.*s", attr->name_size, attr->name);
            break;
        }
    }

    if (!hub) {
        err = knd_attr_hub_new(mempool, &hub);                                    RET_ERR();
        hub->attr = attr;
        hub->next = *hubs;
        *hubs = hub;
    }

    if (item->parent) {
        err = update_attr_hub(topic, spec, &hub->parent,
                              item->parent, item->parent->attr,
                              task, is_ancestor);                                 RET_ERR();
        return knd_OK;
    }

    if (DEBUG_ATTR_INDEX_LEVEL_2)
        knd_log("++ terminal attr hub reached!");

    set = hub->topics;
    if (!set) {
        err = knd_set_new(mempool, &set);                                         RET_ERR();
        hub->topics = set;
    }
    err = set->add(set, topic->entry->id, topic->entry->id_size,
                   (void*)topic->entry);                                          RET_ERR();

    /* register specs? */
    if (!is_ancestor) return knd_OK;

    set = hub->specs;
    if (!set) {
        err = knd_set_new(mempool, &set);                                         RET_ERR();
        hub->specs = set;
    }
    err = set->add(set, spec->entry->id, spec->entry->id_size,
                   (void*)spec->entry);                                           RET_ERR();

    return knd_OK;
}

static int index_inner_class_ref(struct kndClass   *self,
                                 struct kndAttrVar *item,
                                 struct kndAttr *attr,
                                 struct kndTask *task)
{
    struct kndClass *base = attr->ref_class;
    struct kndClass *topic = task->attr->parent_class;
    struct kndClass *spec = item->class;
    struct kndClassRef *ref;
    struct kndClass *curr_class;
    int err;

    if (DEBUG_ATTR_INDEX_LEVEL_2) {
        knd_log("\n.. index path from \"%.*s\" (template:\"%.*s\")",
                spec->name_size, spec->name,
                base->name_size, base->name);
        knd_log("   as \"%.*s\"", attr->name_size, attr->name);
        str_hub_path(item);
        knd_log("   of \"%.*s\" (desc of: %.*s)",
                self->name_size, self->name,
                topic->name_size, topic->name);
    }

    err = update_attr_hub(self, spec, &spec->entry->attr_hubs,
                          item, attr, task, false);                               RET_ERR();

    /* update the ancestors */
    for (ref = spec->entry->ancestors; ref; ref = ref->next) {
        curr_class = ref->class;
        err = knd_is_base(base, curr_class);
        if (err) continue;
        err = update_attr_hub(self, spec, &curr_class->entry->attr_hubs,
                              item, attr, task, true);                            RET_ERR();
    }

    return knd_OK;
}

static int index_attr_item(struct kndClass *self,
                           struct kndAttr *attr,
                           struct kndAttrVar *item,
                           struct kndTask *task)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    int err;

    switch (attr->type) {
    case KND_ATTR_NUM:
        if (!attr->is_indexed) break;

        if (DEBUG_ATTR_INDEX_LEVEL_2)
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
        if (DEBUG_ATTR_INDEX_LEVEL_2)
            knd_log("== nested inner item found: %.*s",
                    item->name_size, item->name);
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

extern int knd_index_inner_attr_var(struct kndClass *self,
                                    struct kndAttrVar *parent_item,
                                    struct kndTask *task)
{
    struct kndAttrVar *item;
    int err;

    if (DEBUG_ATTR_INDEX_LEVEL_2)
        knd_log(".. index inner attr var \"%.*s\" (val:%.*s) "
                " in class \"%.*s\" is list item:%d..",
                parent_item->name_size, parent_item->name,
                parent_item->val_size, parent_item->val,
                self->name_size, self->name,
                parent_item->is_list_item);

    if (parent_item->implied_attr) {
        err = index_attr_item(self, parent_item->implied_attr,
                              parent_item, task);                                 RET_ERR();
    }

    /* index nested children */
    for (item = parent_item->children; item; item = item->next) {
        err = index_attr_item(self, item->attr, item, task);                      RET_ERR();
    }
    return knd_OK;
}
