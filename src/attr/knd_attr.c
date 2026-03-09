#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_facet.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_proc_call.h"
#include "knd_task.h"
#include "knd_class.h"
#include "knd_text.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_LEVEL_1 0
#define DEBUG_ATTR_LEVEL_2 0
#define DEBUG_ATTR_LEVEL_3 0
#define DEBUG_ATTR_LEVEL_4 0
#define DEBUG_ATTR_LEVEL_5 0
#define DEBUG_ATTR_LEVEL_TMP 1

void append_inner_hash_spec(struct kndClassInnerAttr *attr, struct kndFacetHashSpec *spec)
{
    if (attr->hash_specs_tail) {
        attr->hash_specs_tail->next = spec;
        attr->hash_specs_tail = spec;
    } else {
        attr->hash_specs = spec;   
        attr->hash_specs_tail = spec;   
    }
    attr->num_hash_specs++;    
}

void append_hash_spec(struct kndClassRefAttr *attr, struct kndFacetHashSpec *spec)
{
    if (attr->hash_specs_tail) {
        attr->hash_specs_tail->next = spec;
        attr->hash_specs_tail = spec;
    } else {
        attr->hash_specs = spec;   
        attr->hash_specs_tail = spec;   
    }
    attr->num_hash_specs++;    
}

void knd_attr_str(struct kndAttr *self, size_t depth)
{
    struct kndText *tr;
    const char *type_name = knd_attr_names[self->type];

    if (self->is_a_set)
        knd_log("\n%*s[%.*s", depth * KND_OFFSET_SIZE, "",
                self->name_size, self->name);
    else
        knd_log("\n%*s{%s %.*s", depth * KND_OFFSET_SIZE, "",
                type_name, self->name_size, self->name);

    if (self->quant_type == KND_ATTR_SET) {
        knd_log("%*s  QUANT:SET",
                depth * KND_OFFSET_SIZE, "");
    }

    tr = self->tr;
    while (tr) {
        knd_log("%*s   ~ %s %.*s",
                depth * KND_OFFSET_SIZE, "", tr->locale, tr->seq->val_size, tr->seq->val);
        tr = tr->next;
    }

    if (self->cls_name_size) {
        knd_log("%*s  REF class template: %.*s",
                depth * KND_OFFSET_SIZE, "",
                self->cls_name_size, self->cls_name);
    }
    if (self->is_a_set)
        knd_log("%*s]", depth * KND_OFFSET_SIZE, "");
    else
        knd_log("%*s}",  depth * KND_OFFSET_SIZE, "");
}

static int get_immediate_attr(struct kndClass *owner, const char *id, size_t id_size,
                              struct kndAttr **result)
{
    struct kndAttr *attr = NULL;
    FOREACH (attr, owner->attrs) {
        if (!memcmp(attr->id, id, id_size)) {
            *result = attr;
            return knd_OK;
        }
    }
    return knd_NO_MATCH;
}

int knd_attr_find(struct kndClass *cls, const char *name, size_t name_size,
                  struct kndAttr **result,
                  struct kndRepo *repo, struct kndTask *task)
{
    struct kndDict *attr_name_idx = task->idxs.attr_name_idx;
    struct kndAttrRef *refs, *ref = NULL;
    struct kndAttr *attr = NULL;
    struct kndClassEntry *entry;
    struct kndClass *c;
    int err;

    err = knd_dict_get(attr_name_idx, name, name_size, (void**)&refs, task);
    KND_TASK_ERR("no such {attr %.*s}", name_size, name);

    FOREACH (ref, refs) {
        if (ref->cls_entry) {
            entry = ref->cls_entry;
        } else {
            assert (ref->owner_id_size != 0 && ref->owner_id != NULL);

            err = knd_set_get(task->idxs.cls_idx, ref->owner_id, ref->owner_id_size,
                              (void**)&entry, task);
            KND_TASK_ERR("failed to get a {cls-entry %.*s}",
                         ref->owner_id_size, ref->owner_id);
        }

        /* direct owner for this attr */
        if (entry == cls->entry) {
            if (ref->attr) {
                attr = ref->attr;
            } else {
                //knd_log("immediate {attr %.*s}", ref->id_size, ref->id);

                err = get_immediate_attr(cls, ref->id, ref->id_size, &attr);
                KND_TASK_ERR("no immediate {attr %.*s} in {class %.*s}",
                             ref->name_size, ref->name, cls->name_size, cls->name);
            }
            break;
        }

        err = knd_class_acquire(entry, &c, repo, task);
        KND_TASK_ERR("failed to acquire {cls-entry %.*s}", entry->name_size, entry->name);

        err = knd_class_is_base(c, cls);
        if (err) continue;

        if (ref->attr) {
            attr = ref->attr;
            break;
        }

        /* get attr from owner class */
        err = get_immediate_attr(cls, ref->id, ref->id_size, &attr);
        KND_TASK_ERR("no immediate {attr %.*s} in {cls %.*s}",
                     ref->name_size, ref->name, cls->name_size, cls->name);
        break;
    }

    if (!attr) {
        err = knd_NO_MATCH;
        KND_TASK_ERR("{cls %.*s} has no {attr %.*s}",
                     cls->name_size, cls->name, name_size, name);
    }

    *result = attr;
    return knd_OK;
}

int knd_attr_register(struct kndAttr *attr, struct kndClass *cls, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndDict *attr_name_idx = task->idxs.attr_name_idx;
    struct kndSet *attr_idx = task->idxs.attr_idx;
    struct kndAttrRef *attr_ref, *attr_refs;
    const char *name = attr->name;
    size_t name_size = attr->name_size;
    int err;

    if (DEBUG_ATTR_LEVEL_2) {
        knd_log(".. register {cls %.*s {attr %.*s}}",
                cls->name_size, cls->name, name_size, name);
    }

    attr->numid = ++task->idxs.attr_id_count;
    knd_uid_create(attr->numid, attr->id, &attr->id_size);

    err = knd_attr_ref_new(&attr_ref, mempool);
    KND_TASK_ERR("failed to alloc kndAttrRef")
    attr_ref->attr = attr;
    attr_ref->cls_entry = cls->entry;

    switch (task->type) {
    case KND_TASK_RESTORE:
        // fall through
    case KND_TASK_BULK_LOAD:
        err = knd_dict_get(attr_name_idx, name, name_size, (void**)&attr_refs, task);
        switch (err) {
        case knd_OK:
            if (attr_refs->tail) {
                attr_refs->tail->next = attr_ref;
                attr_refs->tail = attr_ref;
            } else {
                attr_refs->next = attr_ref;
            }
            attr_refs->tail = attr_ref;
            break;
        case knd_NO_MATCH:
            err = knd_dict_set(attr_name_idx, attr->name, attr->name_size, (void*)attr_ref, task);
            KND_TASK_ERR("failed to globally register {attr %.*s}", name_size, name);
            break;
        default:
            return err;
        }

        err = knd_set_add(attr_idx, attr->id, attr->id_size, (void*)attr_ref, task);
        KND_TASK_ERR("failed to globally register numid of {attr %.*s}", name_size, name);

        err = knd_set_add(cls->attr_idx, attr->id, attr->id_size, (void*)attr_ref, task);
        KND_TASK_ERR("failed to locally register numid of {attr %.*s}", name_size, name);

        return knd_OK;
    default:
        break;
    }

    /* local task name idx */
    err = knd_dict_set(task->idxs.attr_name_idx, name, name_size, (void*)attr_ref, task);
    KND_TASK_ERR("failed to register {attr %.*s}", name_size, name);

    if (DEBUG_ATTR_LEVEL_2) {
        knd_log("++ commit import: new primary {attr %.*s {id %.*s}}",
                name_size, name, attr->id_size, attr->id);
    }
    return knd_OK;
}

int knd_attr_export(struct kndAttr *self, knd_format format, struct kndRepo *repo, struct kndTask *task)
{
    switch (format) {
    case KND_FORMAT_JSON:
        return knd_attr_export_JSON(self, repo, task, 0);
    case KND_FORMAT_GSP:
        return knd_attr_export_GSP(self, repo, task);
    default:
        break;
    }
    return knd_NO_MATCH;
}


int knd_attr_ref_new(struct kndAttrRef **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndAttrRef));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrRef));
    *result = page;
    return knd_OK;
}

int knd_cls_inner_attr_new(struct kndClassInnerAttr **result,
                           const char *name, size_t name_size, struct kndMemPool *mempool)
{
    struct kndClassInnerAttr *inner;
    struct kndFacetHashSpec *spec;
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndClassInnerAttr));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassInnerAttr));
    inner = page;
    inner->name = name;
    inner->name_size = name_size;

    err = knd_facet_hash_spec_new(&spec, KND_FACET_CLS,
                                  knd_facet_cls_key_get,
                                  knd_cls_facet_key_encode,
                                  knd_facet_cls_key_str,
                                  knd_facet_cls_hash, mempool);
    if (err) return err;

    append_inner_hash_spec(inner, spec);

    *result = inner;
    return knd_OK;
}

int knd_cls_ref_attr_new(struct kndClassRefAttr **result,
                         const char *name, size_t name_size,
                         struct kndMemPool *mempool)
{
    struct kndClassRefAttr *refattr;
    struct kndFacetHashSpec *spec;
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndClassRefAttr));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassRefAttr));
    refattr = page;
    refattr->name = name;
    refattr->name_size = name_size;

    err = knd_facet_hash_spec_new(&spec, KND_FACET_CLS,
                                  knd_facet_cls_key_get,
                                  knd_cls_facet_key_encode,
                                  knd_facet_cls_key_str,
                                  knd_facet_cls_hash, mempool);
    if (err) return err;

    append_hash_spec(refattr, spec);

    *result = refattr;
    return knd_OK;
}

int knd_cls_inst_ref_attr_new(struct kndClassInstRefAttr **result,
                              const char *name, size_t name_size, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndClassInstRefAttr));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassInstRefAttr));
    *result = page;
    (*result)->name = name;
    (*result)->name_size = name_size;
    return knd_OK;
}

int knd_attr_new(struct kndAttr **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_x2_page_size >= sizeof(struct kndAttr));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X2, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttr));
    *result = page;
    return knd_OK;
}
