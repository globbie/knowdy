#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_attr.h"
#include "knd_attr_stm.h"
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

    if (self->concise_level) {
        knd_log("%*s  CONCISE:%zu",
                depth * KND_OFFSET_SIZE, "", self->concise_level);
    }


    if (self->is_implied) {
        knd_log("%*s  (implied)",
                depth * KND_OFFSET_SIZE, "");
    }

    tr = self->tr;
    while (tr) {
        knd_log("%*s   ~ %s %.*s",
                depth * KND_OFFSET_SIZE, "", tr->locale, tr->seq->val_size, tr->seq->val);
        tr = tr->next;
    }

    if (self->classname_size) {
        knd_log("%*s  REF class template: %.*s",
                depth * KND_OFFSET_SIZE, "",
                self->classname_size, self->classname);
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
                  struct kndAttr **result, struct kndTask *task)
{
    struct kndSharedDict *attr_name_idx = task->idxs->attr_name_idx;
    struct kndAttrRef *refs, *ref = NULL;
    struct kndAttr *attr = NULL;
    struct kndClassEntry *entry;
    struct kndClass *c;
    int err;

    refs = knd_shared_dict_get(attr_name_idx, name, name_size);
    if (!refs) {
        err = knd_NO_MATCH;
        KND_TASK_ERR("no such attr %.*s", name_size, name);
    }

    FOREACH (ref, refs) {
        if (ref->class_entry) {
            entry = ref->class_entry;
        } else {
            assert (ref->owner_id_size != 0 && ref->owner_id != NULL);
            err = knd_shared_set_get(task->idxs->class_idx,
                                     ref->owner_id, ref->owner_id_size, (void**)&entry);
            KND_TASK_ERR("failed to get a {class-entry %.*s}",
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

        err = knd_class_acquire(entry, &c, task);
        KND_TASK_ERR("failed to acquire class {entry %.*s}",
                     entry->name_size, entry->name);

        err = knd_is_base(c, cls);
        if (err) continue;

        if (ref->attr) {
            attr = ref->attr;
            break;
        }

        /* get attr from owner class */
        err = get_immediate_attr(cls, ref->id, ref->id_size, &attr);
        KND_TASK_ERR("no immediate {attr %.*s} in {class %.*s}",
                     ref->name_size, ref->name, cls->name_size, cls->name);
        break;
    }

    if (!attr) {
        err = knd_NO_MATCH;
        KND_TASK_ERR("class %.*s has no attr %.*s", cls->name_size, cls->name, name_size, name);
    }

    *result = attr;
    return knd_OK;
}

int knd_attr_export(struct kndAttr *self, knd_format format, struct kndTask *task)
{
    switch (format) {
    case KND_FORMAT_JSON:
        return knd_attr_export_JSON(self, task, 0);
    case KND_FORMAT_GSP:
        return knd_attr_export_GSP(self, task);
    default:
        break;
    }
    return knd_NO_MATCH;
}

int knd_attr_stm_new(struct kndAttrStm **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_x4_page_size >= sizeof(struct kndAttrStm));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X4, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrStm));
    *result = page;
    return knd_OK;
}

int knd_attr_facet_elem_new(struct kndAttrFacetElem **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndAttrFacetElem));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrFacetElem));
    *result = page;
    return knd_OK;
}

int knd_attr_facet_elem_idx_new(struct kndAttrFacetElemIdx **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_x4_page_size >= sizeof(struct kndAttrFacetElemIdx));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X4, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrFacetElemIdx));
    *result = page;
    return knd_OK;
}

int knd_attr_facet_new(struct kndAttrFacet **result, knd_attr_facet_type type, struct kndMemPool *mempool)
{
    struct kndAttrFacetElemIdx *elems;
    void *page;
    int err;

    err = knd_attr_facet_elem_idx_new(&elems, mempool);
    if (err) return err;

    assert(mempool->page_size >= sizeof(struct kndAttrFacet));
    err = knd_mempool_page(mempool, KND_MEMPAGE_BASE, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrFacet));

    *result = page;
    (*result)->type = type;
    (*result)->elems = elems;
    return knd_OK;
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

int knd_cls_ref_attr_new(struct kndClassRefAttr **result,
                         const char *name, size_t name_size, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndClassRefAttr));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassRefAttr));
    *result = page;
    (*result)->name = name;
    (*result)->name_size = name_size;
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
