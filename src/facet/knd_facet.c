#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_class.h"
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
                   knd_facet_map_cb cb, size_t depth)
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
            cb(facet->cache[i], &depth);
        }
    }

    if (facet->idx) {
        err = knd_set_map(facet->idx, cb, &depth);
        if (err) return;
    }

    if (facet->num_children) {
        for (size_t i = 0; i < KND_MAX_FACETS; i++) {
            if (!facet->children[i]) continue;
            knd_facet_str(facet->children[i], cb, depth);
        }
    }

    depth--;
    knd_log("%*s}", depth * KND_OFFSET_SIZE, "");
}

int knd_facet_hash_spec_new(struct kndFacetHashSpec **result, knd_facet_type facet_type,
                            knd_facet_key_get_cb key_get_cb,
                            knd_facet_key_str_cb key_str_cb,
                            knd_facet_hash_cb hash_cb,
                            struct kndMemPool *mempool)
{
    void *page;
    struct kndFacetHashSpec *spec;
    int err;

    assert(mempool->tiny_page_size >= sizeof(struct kndFacetHashSpec));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndFacetHashSpec));
    spec = page;

    spec->type = facet_type;

    spec->key_get_cb = key_get_cb;
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

    assert(mempool->base_page_size >= sizeof(struct kndFacet));

    err = knd_mempool_page(mempool, KND_MEMPAGE_BASE, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndFacet));
    f = page;

    f->key = key;
    f->hash_specs = hash_specs;
    f->num_hash_specs = num_hash_specs;

    f->elem_id_cb = elem_id_cb;
    f->curr_spec = hash_specs;

    *result = f;
    return knd_OK;
}

int knd_facet_leaf_new(struct kndFacetLeaf **result, size_t numid, struct kndFacet *f)
{
    struct kndFacetLeaf *leaf;
    leaf = calloc(1, sizeof(struct kndFacetLeaf));
    if (!leaf) return knd_NOMEM;

    leaf->numid = numid;
    leaf->parent = f;

    leaf->min_leaf_size = KND_SNAPSHOT_LEAF_MIN_THRESHOLD;
    leaf->max_leaf_size = KND_SNAPSHOT_LEAF_MAX_THRESHOLD;

    *result = leaf;
    return knd_OK;
}
