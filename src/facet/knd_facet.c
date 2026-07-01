#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_class.h"
#include "knd_quant.h"
#include "knd_task.h"
#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_config.h"

#define DEBUG_FACET_LEVEL_1 0
#define DEBUG_FACET_LEVEL_2 0
#define DEBUG_FACET_LEVEL_3 0
#define DEBUG_FACET_LEVEL_4 0
#define DEBUG_FACET_LEVEL_5 0
#define DEBUG_FACET_LEVEL_TMP 1

void knd_facet_str(struct kndFacet *facet,
                   map_cb_t cb, struct kndTask *task, size_t depth)
{
    struct kndFacetHashSpec *spec = facet->hash_specs;
    assert (spec != NULL);
    int err;

    knd_log("%*s{facet {type %s} {num-children %zu} {num-elems %zu}",
            depth * KND_OFFSET_SIZE, "",
            knd_facet_type_names[spec->type],
            facet->num_children, facet->num_elems);

    depth++;

    if (facet->key) {
        knd_log("%*s{key ", (depth) * KND_OFFSET_SIZE, "");
        spec->key_str_cb(facet->key, depth + 2);
        knd_log("%*s}", (depth) * KND_OFFSET_SIZE, "");
    } else {
        knd_log("{root}");
    }

    if (facet->cache_size) {
        for (size_t i = 0; i < facet->cache_size; i++) {
            cb(facet->cache[i], &depth, task);
        }
    }

    if (facet->idx) {
        err = knd_set_map(facet->idx, NULL, NULL, NULL, cb, &depth, task);
        if (err) return;
    }

    if (facet->num_children) {
        for (size_t i = 0; i < KND_MAX_FACETS; i++) {
            if (!facet->children[i]) continue;
            knd_facet_str(facet->children[i], cb, task, depth);
        }
    }

    depth--;
    knd_log("%*s}", depth * KND_OFFSET_SIZE, "");
}

int knd_facet_acquire(struct kndAttr *attr, struct kndFacet **result,
                      struct kndRepoSnapshot *unused_var(snapshot), struct kndTask *task)
{
    struct kndQuantAttr *quant_attr;
    struct kndClassInnerAttr *cls_inner_attr;
    struct kndClassRefAttr *cls_ref_attr;
    int err;

    /* check global cache */
    if (attr->facet) {
        *result = attr->facet;
        return knd_OK;
    }

    switch (attr->type) {
    case KND_ATTR_UINT:
        quant_attr = attr->subtype;
        if (!attr->facet) {
            err = knd_facet_new(&attr->facet, NULL,
                                quant_attr->hash_specs, quant_attr->num_hash_specs,
                                knd_attr_stm_get_elem_key, task->mempool);
            KND_TASK_ERR("failed to alloc a facet");
        }
        break;
    case KND_ATTR_URATIO:
        //err = knd_quant_uint_index(attr->impl, entry, stm, task);
        //KND_TASK_ERR("failed to index natural number attr stm");
        break;
    case KND_ATTR_UREAL:
        //err = knd_quant_ureal_index(facet, entry, stm, task);
        //KND_TASK_ERR("failed to index real number attr stm");
        break;
    case KND_ATTR_STR:
        break;
    case KND_ATTR_CLS_INNER:
        cls_inner_attr = attr->subtype;
        assert (cls_inner_attr != NULL);
        if (!attr->facet) {
            err = knd_facet_new(&attr->facet, cls_inner_attr->template_cls,
                                cls_inner_attr->hash_specs, cls_inner_attr->num_hash_specs,
                                knd_attr_stm_get_elem_key, task->mempool);
            KND_TASK_ERR("failed to alloc a facet");
        }
        break;
    case KND_ATTR_CLS_REF:
        cls_ref_attr = attr->subtype;
        if (!attr->facet) {
            err = knd_facet_new(&attr->facet, cls_ref_attr->template_cls,
                                cls_ref_attr->hash_specs, cls_ref_attr->num_hash_specs,
                                knd_attr_stm_get_elem_key, task->mempool);
            KND_TASK_ERR("failed to alloc a facet");
        }
        break;
    default:
        break;
    }

    /*err = knd_shared_set_find_leaf(task->idxs->attr_idx, entry->id, entry->id_size, &leaf, task);
    KND_TASK_ERR("no storage leaf found for unmarshalling {cls %.*s}", entry->id_size, entry->id);

    err = knd_storage_leaf_read_elem(leaf, entry->id, entry->id_size,
                                     knd_class_unmarshall, entry, (void**)&c, task);
    KND_TASK_ERR("failed to read {cls %.*s}", entry->name_size, entry->name);

    err = knd_class_decode(c, task);
    KND_TASK_ERR("failed to decode {cls %.*s}", c->name_size, c->name);
    */

    return knd_OK;
}

int knd_facet_hash_spec_new(struct kndFacetHashSpec **result, knd_facet_type facet_type,
                            knd_facet_key_get_cb key_get_cb,
                            knd_facet_key_encode_cb key_encode_cb,
                            knd_facet_key_str_cb key_str_cb,
                            knd_facet_hash_cb hash_cb,
                            struct kndMemPool *mempool)
{
    void *page;
    struct kndFacetHashSpec *spec;
    int err;

    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndFacetHashSpec));

    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndFacetHashSpec));
    spec = page;

    spec->type = facet_type;

    spec->key_get_cb = key_get_cb;
    spec->key_encode_cb = key_encode_cb;
    spec->key_str_cb = key_str_cb;
    spec->hash_cb = hash_cb;

    *result = spec;
    return knd_OK;
}

int knd_facet_new(struct kndFacet **result, void *key,
                  struct kndFacetHashSpec *hash_specs, size_t num_hash_specs,
                  knd_facet_elem_id_cb elem_id_cb, struct kndMemPool *mempool)
{
    struct kndFacet *f;
    void *page;
    int err;

    assert(KND_BASE_MEMPAGE_SIZE >= sizeof(struct kndFacet));

    err = knd_mempool_page(mempool, KND_MEMPAGE_BASE, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndFacet));
    f = page;

    f->key = key;
    f->hash_specs = hash_specs;
    f->num_hash_specs = num_hash_specs;

    f->elem_id_cb = elem_id_cb;
    f->curr_spec = hash_specs;

    *result = f;
    return knd_OK;
}
