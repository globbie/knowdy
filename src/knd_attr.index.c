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
#include <glb-lib/output.h>

#define DEBUG_ATTR_INDEX_LEVEL_1 0
#define DEBUG_ATTR_INDEX_LEVEL_2 0
#define DEBUG_ATTR_INDEX_LEVEL_3 0
#define DEBUG_ATTR_INDEX_LEVEL_4 0
#define DEBUG_ATTR_INDEX_LEVEL_5 0
#define DEBUG_ATTR_INDEX_LEVEL_TMP 1

/*static int kndAttr_add_reverse_link(struct kndFacet  *self,
                                    struct kndClassEntry *base,
                                    struct kndSet  *unused_var(set),
                                    struct kndMemPool *mempool)
{
    struct kndClassEntry *topic = self->attr->parent_class->entry;
    struct kndAttr *attr = self->attr;
    struct kndClassRel *class_rel;
    int err;

    assert(attr != NULL);

    err = knd_class_rel_new(mempool, &class_rel);                                  RET_ERR();

    //class_rel->topic = topic;
    class_rel->attr = attr;

    class_rel->next =  base->reverse_rels;
    base->reverse_rels = class_rel;

    if (DEBUG_ATTR_INDEX_LEVEL_2) {
        knd_log(".. attr %.*s:  add \"%.*s\" to reverse idx of \"%.*s\"..",
                attr->name_size,  attr->name,
                topic->name_size, topic->name,
                base->name_size,  base->name);
    }

    return knd_OK;
}
*/

extern int knd_index_attr(struct kndClass *self,
                          struct kndAttr *attr,
                          struct kndAttrVar *item,
                          struct kndTask *task)
{
    struct kndClass *base;
    struct kndSet *set;
    struct kndClass *c = NULL;
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

    /* specific class */
    err = knd_get_class(self->entry->repo,
                        item->val,
                        item->val_size, &c, task);                                RET_ERR();

    item->class = c;

    if (!c->is_resolved) {
        err = knd_class_resolve(c, task);                                         RET_ERR();
    }

    err = knd_is_base(base, c);                                                   RET_ERR();

    set = attr->parent_class->entry->descendants;

    /* add curr class to the reverse index */
    //err = knd_set_add_ref(set, attr, self->entry, c->entry);
    //if (err) return err;

    return knd_OK;
}

extern int knd_index_attr_var_list(struct kndClass *self,
                                   struct kndAttr *attr,
                                   struct kndAttrVar *parent_item,
                                   struct kndTask *task)
{
    struct kndClass *base;
    struct kndSet *set;
    struct kndClass *c, *idx_class, *curr_class;
    struct kndClassRef *ref;
    struct kndAttrVar *item = parent_item;
    struct kndMemPool *mempool = task->mempool;
    const char *name;
    size_t name_size;
    int err;

    if (DEBUG_ATTR_INDEX_LEVEL_2) {
        knd_log("\n.. attr item list indexing.. (class:%.*s) "
                ".. index attr: \"%.*s\" [type:%d]"
                " refclass: \"%.*s\" (name:%.*s val:%.*s)",
                self->entry->name_size, self->entry->name,
                attr->name_size, attr->name, attr->type,
                attr->ref_classname_size, attr->ref_classname,
                item->name_size, item->name, item->val_size, item->val);
    }

    switch (attr->type) {
    case KND_ATTR_INNER:
        knd_log("..indexing inner class..\n");

        break;
    default:
        break;
    }

    if (!attr->ref_classname_size) return knd_OK;

    /* template base class */
    err = knd_get_class(self->entry->repo,
                        attr->ref_classname,
                        attr->ref_classname_size,
                        &base, task);                                                 RET_ERR();
    if (!base->is_resolved) {
        err = knd_class_resolve(base, task);                                          RET_ERR();
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
        
        /* specific class */
        err = knd_get_class(self->entry->repo,
                            name,
                            name_size, &c, task);                                 RET_ERR();
        item->class = c;

        if (!c->is_resolved) {
            err = knd_class_resolve(c, task);                                           RET_ERR();
        }

        err = knd_is_base(base, c);
        if (err) {
            knd_log("-- no inheritance from \"%.*s\" to \"%.*s\"",
                    base->name_size, base->name, c->name_size, c->name);
            return err;
        }

        idx_class = attr->parent_class;

        if (idx_class->entry->repo != self->entry->repo) {
            for (ref = self->entry->ancestors; ref; ref = ref->next) {
                curr_class = ref->class;
                if (curr_class->entry->orig == idx_class->entry) {
                    idx_class = curr_class;
                }
            }

        }

        set = idx_class->entry->descendants;
        if (!set) {
            err = knd_set_new(mempool, &set);                                     RET_ERR();
            set->type = KND_SET_CLASS;
            set->base =  idx_class->entry;
            idx_class->entry->descendants = set;
        }

        if (DEBUG_ATTR_INDEX_LEVEL_2)
            knd_log("\n.. add %.*s ref to %.*s (repo:%.*s)",
                    item->name_size, item->name,
                    idx_class->name_size,
                    idx_class->name,
                    idx_class->entry->repo->name_size,
                    idx_class->entry->repo->name);

        /* add curr class to the reverse index */
        //err = knd_set_add_ref(set, attr, self->entry, c->entry);
        //if (err) return err;
    }
    return knd_OK;
}

static void str_rel_path(struct kndAttrVar *item)
{
    if (!item->parent) return;
    knd_log("   in \"%.*s\"", item->parent->name_size, item->parent->name);
    str_rel_path(item->parent);
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

    if (DEBUG_ATTR_INDEX_LEVEL_TMP) {
        knd_log("\n.. index rel from \"%.*s\" (template:\"%.*s\")",
                spec->name_size, spec->name,
                base->name_size, base->name);
        knd_log("   as \"%.*s\"", attr->name_size, attr->name);
        str_rel_path(item);

        knd_log("   of \"%.*s\" (desc of: %.*s)",
                self->name_size, self->name,
                topic->name_size, topic->name);
    }

    for (ref = spec->entry->ancestors; ref; ref = ref->next) {
        curr_class = ref->class;
        err = knd_is_base(base, curr_class);
        if (err) continue;


        knd_log("  .. update ancestor: %.*s",
                curr_class->name_size, curr_class->name);

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

        if (DEBUG_ATTR_INDEX_LEVEL_TMP)
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
    //struct kndClass *c;
    struct kndAttrVar *item;
    //struct kndAttr *attr;
    //const char *classname;
    //size_t classname_size;
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
